/**************************************************************************************
*  File name:      Fatigue_Tester.c
*  Author:         Mxin Chiang
*  Version:        1.0
*  Date:           12.28.2015
*  Description:    Design a software accepts data sent from fatigue testing machine,
*                  waveform presentation, recording in MySQL database,
*                  data processing and generate pdf reports.
*  Others:
*  Function List:
***************************************************************************************/
#define _CRT_SECURE_NO_DEPRECATE
#include <gtk-3.0\gtk\gtk.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <cairo-pdf.h>
#include <math.h>
#include "socket_msg.h"

#define _WIN32_ 1    /* Compile for WIN32 */
//#define _LINUX_ 1    /* Compile for Linux */

#ifdef _WIN32_
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include "mysql.h"
#endif

#ifdef _LINUX_
#include <unistd.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <semaphore.h>
#include <mysql/mysql.h>
#endif

#define SERVER_HOST "localhost"
#define SERVER_USER "root"
#define SERVER_PWD  "12345"

#define DB_NAME     "fatigue_test_db"
#define TABLE_NAME  "mytables"

#define M_PI 3.1415926
#define FILTER_N 100


//#define CAIRO_HAS_PDF_SURFACE 1

gint check_tbl(MYSQL* mysql, gchar *name);
gint check_db(MYSQL *mysql, gchar *db_name);

gint **datas;
gint num = 0;
time_t now;
struct tm *curTime;

GSocket *sock;
gint issucceed = -1;
#define MAXSIZE 2048
GtkTextBuffer *show_buffer, *input_buffer;
gboolean timer = TRUE;
static cairo_surface_t *surface = NULL;
static cairo_surface_t *surface2 = NULL;
static cairo_surface_t *surface3 = NULL;
GtkWidget *report_window = NULL;
GtkWidget *ip_menu_window = NULL;

gint last_point[8];
gint biggest = 0;
gint top_y = 50;
gboolean has_max = FALSE;
gboolean has_min = FALSE;
gboolean has_run_time = FALSE;
gboolean has_date_time = FALSE;
gdouble arc_i = 0.0;

gboolean buffer1_ready = true;
gboolean buffer2_ready = true;

gint recv_num=0;
gint filter_buf[8][101];

struct EntryStruct
{
	GtkEntry *IP;
	GtkEntry *Port;
};

struct EntryStruct1
{
	GtkEntry *DA1;
	GtkEntry *DA2;
	GtkEntry *D0;
	GtkEntry *PWM;
	GtkEntry *PWM_Duty;
	GtkEntry *PWM_DIR;
};

gchar *se_ip=g_new(gchar,1);
gchar *se_port=g_new(gchar,1);
struct EntryStruct entries;

gdouble time_second = 0.0;
gboolean start=false;

gchar *_(gchar *c)
{
	return(g_locale_to_utf8(c, -1,NULL, NULL, NULL));
}

/***************************************************************************************
*    Function:
*    Description:  socket msg parse
***************************************************************************************/
//initialize the socket_msg structure
void socket_msg_init(socket_msg *msg)
{
	msg->len = 0;
	msg->type = 0;
}

//initialize the socket_cache structure
void socket_cache_init(socket_cache *cache, tp_socket_msg_handle handle)
{
	cache->front = 0;
	cache->rear = 0;
	cache->current = 0;
	cache->len = 0;
	cache->tag = 0;
	cache->strategy = SEARCH_HEAD;
	cache->handle = handle;
	cache->args = NULL;

	socket_msg_init(&cache->recv_msg);
}

//copy buffer to cache from buffer
gint socket_msg_cpy_in(socket_cache *cache, guchar *buf, gint len)
{
	gint left_len;
	gint copy_len;
	gint count;
	guchar *src = buf;

	if (cache->tag == 1 && cache->front == cache->rear) {
		g_print("socket_msg_cpy_in:socket cache is full!\n");
		return 	FALSE;
	}

	left_len = SOCKET_MSG_CACHE_SIZE - cache->len;
	copy_len = len > left_len ? left_len : len;
	count = copy_len;

	while (count--)
	{
		*(cache->buf + cache->rear) = *src;
		src++;
		cache->rear = (cache->rear + 1) % SOCKET_MSG_CACHE_SIZE;
		cache->len++;
		cache->tag = 1;

	}
	return copy_len;
}

//copy data to buffer from cache
gint socket_msg_cpy_out(socket_cache *cache, guchar *buf, gint start_index, gint len)
{
	guchar* dest;
	gint src;
	gint count;
	if (cache == NULL || cache->buf == NULL || buf == NULL || len == 0) {
		return FALSE;
	}

	if (cache->front == cache->rear && cache->tag == 0) {
		g_print("socket_msg_cpy_out:socket cache is empty!\n");
		return FALSE;
	}

	if (cache->rear > cache->front) {
		if (start_index < cache->front || start_index > cache->rear) {
			g_print("socket_msg_cpy_out:invalid start index!\n");
			return FALSE;
		}
	}
	else if (start_index > cache->rear && start_index < cache->front) {
		g_print("socket_msg_cpy_out:invalid start index!\n");
		return FALSE;
	}

	src = start_index;
	dest = buf;
	count = len;
	while (count--)
	{
		*dest = *(cache->buf + src);
		dest++;
		src = (src + 1) % SOCKET_MSG_CACHE_SIZE;

	}
	return len;
}

//parsed the packaged data, and invoke callback function
void socket_msg_parse(gint fd, socket_cache *cache)
{
	gint current_len;
	gint p, q, m, n;
	gint i;
	gint find;

	if (cache->front == cache->rear && cache->tag == 0) {
		//D("socket cache is empty!\n");
		return;
	}

	//calculate the current length of cache
	if (cache->current >= cache->front) {
		current_len = cache->len - (cache->current - cache->front);
	}
	else {
		current_len = cache->rear - cache->current;
	}

	switch (cache->strategy) {
	case SEARCH_HEAD://to find a Head format in cache
		if (current_len < SOCKET_MSG_HEAD_SIZE) {
			return;
		}
		find = FALSE;
		for (i = 0; i < current_len - 1; i++) {
			p = cache->current;
			q = (cache->current + 1) % SOCKET_MSG_CACHE_SIZE;
			m = (cache->current + 2) % SOCKET_MSG_CACHE_SIZE;
			n = (cache->current + 3) % SOCKET_MSG_CACHE_SIZE;
			if ((cache->buf[p] == (SOCKET_MSG_HEAD >> 24)) &&
				(cache->buf[q] == (SOCKET_MSG_HEAD >> 16)) &&
				(cache->buf[m] == (SOCKET_MSG_HEAD >> 8)) &&
				(cache->buf[n] == (SOCKET_MSG_HEAD & 0xff))) 
			{

				find = TRUE;
				break; //exit for loop
			}
			else {
				//current pointer move to next
				cache->current = q;
				//delete one item
				cache->front = cache->current;
				cache->len--;
				cache->tag = 0;
			}
		}

		if (find == TRUE) {
			//move 2 items towards next
			cache->current = (cache->current + 2) % SOCKET_MSG_CACHE_SIZE;
			//we found the head format, go on to find Type byte
			cache->strategy = SEARCH_TYPE;
		}
		else {
			//if there is no head format ,delete previouse items
			g_print("socket message without head: %x!\n", SOCKET_MSG_HEAD);
			//go on to find Head format
			cache->strategy = SEARCH_HEAD;
		}
		break;

	case SEARCH_FIRST://to find the type byte in cache
		if (current_len < SOCKET_MSG_FIRST_SIZE) {
			return;
		}
		//get the value of type
		//cache->type = cache->buf[cache->current];
		cache->recv_msg.type = cache->buf[cache->current];
		cache->current = (cache->current + 1) % SOCKET_MSG_CACHE_SIZE;
		//we found Type byte, go on to find Datalen format
		cache->strategy = SEARCH_TYPE;
		break;

	case SEARCH_TYPE://to find the type byte in cache
		if (current_len < SOCKET_MSG_TYPE_SIZE) {
			return;
		}
		//get the value of type
		//cache->type = cache->buf[cache->current];
		cache->recv_msg.type = cache->buf[cache->current];
		cache->current = (cache->current + 1) % SOCKET_MSG_CACHE_SIZE;
		//we found Type byte, go on to find Datalen format
		cache->strategy = SEARCH_SECOND;
		break;

	case SEARCH_SECOND://to find the type byte in cache
		if (current_len < SOCKET_MSG_SECOND_SIZE) {
			return;
		}
		//get the value of type
		//cache->type = cache->buf[cache->current];
		cache->recv_msg.type = cache->buf[cache->current];
		cache->current = (cache->current + 1) % SOCKET_MSG_CACHE_SIZE;
		//we found Type byte, go on to find Datalen format
		cache->strategy = SEARCH_END;
		break;

	case SEARCH_END:
		if (current_len < (cache->recv_msg.len + SOCKET_MSG_END_SIZE)) {
			return;
		}
		//because we have known the data bytes' len, so we move the very  
		//distance of datalen to see if there is End format. 
		p = (cache->current + cache->recv_msg.len) % SOCKET_MSG_CACHE_SIZE;
		q = (cache->current + cache->recv_msg.len + 1) % SOCKET_MSG_CACHE_SIZE;
		m = (cache->current + cache->recv_msg.len + 2) % SOCKET_MSG_CACHE_SIZE;
		n = (cache->current + cache->recv_msg.len + 3) % SOCKET_MSG_CACHE_SIZE;
		if ((cache->buf[p] == (SOCKET_MSG_END >> 24)) &&
			(cache->buf[q] == (SOCKET_MSG_END >> 16)) &&
			(cache->buf[m] == (SOCKET_MSG_END >> 8)) &&
			(cache->buf[n] == (SOCKET_MSG_END & 0xff))) {
			socket_msg_cpy_out(cache, cache->recv_msg.data, cache->current, cache->recv_msg.len);
			if (cache->handle != NULL) {
				//cache->handle(fd, cache->buf + cache->data_index, cache->data_len);
				cache->handle(fd, &cache->recv_msg, cache->args);
			}
			//delete all previous items
			cache->current = (q + 1) % SOCKET_MSG_CACHE_SIZE;
			cache->front = cache->current;
			cache->len -= (cache->recv_msg.len + SOCKET_MSG_FORMAT_SIZE);
			cache->tag = 0;

		}
		else {
			g_print("socket message without end: %x!\n", SOCKET_MSG_END);
			//delete the frist item 'a5'
			//move back 3 items
			cache->current = cache->current >= 3 ? (cache->current - 3) : (SOCKET_MSG_CACHE_SIZE - 3 + cache->current);
			cache->front = cache->current;
			//length sub 3
			cache->len -= 3;
			cache->tag = 0;

		}
		//go on to find Head format
		cache->strategy = SEARCH_HEAD;
		break;

	default:
		break;
	}
	//parse new socket message
	socket_msg_parse(fd, cache);
}

//copy the unparsed data to cache, and parsed them
gint socket_msg_pre_parse(
	gint fd,
	socket_cache *cache,
	guchar *buf,
	gint len,
	void *args)
{
	gint n = 0;
	guchar *p = buf;
	//when reading buffer's length is greater than cache's left length,
	//we should copy many times.
	cache->args = args;
	while (1) {
		n = socket_msg_cpy_in(cache, p, len);
		if (n == 0) {
			return FALSE;//cache is full	
		}
		//parse and handle socket message from cache
		socket_msg_parse(fd, cache);

		if (n == len) {
			return TRUE; //copy completed
		}
		//move the pointer
		p = p + n;
		len = len - n;
	}

	return TRUE;

}

//before you send data,you should package them
void socket_msg_package(socket_msg *msg, guchar type, guchar *buf, gint len)
{
	if (msg == NULL || buf == NULL || len <= 0) {
		return;
	}
	//head
	msg->data[0] = SOCKET_MSG_HEAD >> 8;
	msg->data[1] = SOCKET_MSG_HEAD & 0xff;
	//type
	msg->data[2] = type;
	//data len
	msg->data[3] = len;
	//data
	memcpy(msg->data + 4, buf, len);
	//end
	msg->data[len + 4] = SOCKET_MSG_END >> 8;
	msg->data[len + 5] = SOCKET_MSG_END & 0xff;

	//message len
	msg->len = len + SOCKET_MSG_FORMAT_SIZE;
}

/***************************************************************************************
*    Function:
*    Description:  Filter Operations
***************************************************************************************/
gint *Filter(gchar recv_data[]) 
{
	gint i=0;
	gint j=0;
	gint data[8];
	for(i=0;i<8;i++)
	{
		data[i]=recv_data[i*4]<<24+recv_data[i*4+1]<<16+recv_data[i*4+2]<<8+recv_data[i*4+3];
	}
	if(recv_num>=100)
	{
		for(i=1;i<100;i++)
		{
			for(j=0;j<8;j++)
			{
				filter_buf[i][j]=filter_buf[i-1][j];
			}
		}
		for(i=0;i<8;i++)
		{
			filter_buf[i][100]=data[i];
		}
		for(i=0;i<8;i++)
		{
			data[i]=0;
		}
		for(i=0;i<8;i++)
		{
			for(j=0;j<100;j++)
			{
				data[i]=data[i]+filter_buf[i][j];
			}
			data[i]=data[i]/100;
		}
		return data;
		recv_num=100;
	}
	else if(recv_num<100)
	{
		for(i=0;i<8;i++)
		{
			filter_buf[i][recv_num]=data[i];
		}
		recv_num++;
		return data;
	}
}


/***************************************************************************************
*    Function:
*    Description:  Database Operations
***************************************************************************************/
//
//gint init_db()
//{
//	gint err = 0;
//	MYSQL mysql;
//
//	if (!mysql_init(&mysql))
//	{
//		g_print("mysql_init:");
//		exit(1);
//	}
//
//	if (!mysql_real_connect(&mysql, SERVER_HOST, SERVER_USER, SERVER_PWD, NULL, 0, NULL, 0))
//	{
//		g_print("mysql_real_connect");
//		exit(1);
//	}
//
//	err = check_db(&mysql, DB_NAME);/* Check database */
//	if (err != 0)
//	{
//		g_print("create db is err!\n");
//		mysql_close(&mysql);
//		exit(1);
//	}
//
//	if (mysql_select_db(&mysql, DB_NAME)) /* Select which db */
//	{
//		g_print("mysql_select_db:");
//		mysql_close(&mysql);
//		exit(1);
//	}
//	if ((err = check_tbl(&mysql, TABLE_NAME)) != 0)/* Check table */
//	{
//		g_print("check_tbl is err!\n");
//		mysql_close(&mysql);
//		exit(1);
//	}
//	mysql_close(&mysql);
//	return 0;
//}
//
//gint check_db(MYSQL *mysql, gchar *db_name)
//{
//	MYSQL_ROW row = NULL;
//	MYSQL_RES *res = NULL;
//
//	res = mysql_list_dbs(mysql, NULL);
//	if (res)
//	{
//		while ((row = mysql_fetch_row(res)) != NULL)
//		{
//			g_print("db is %s\n", row[0]);
//			if (strcmp(row[0], db_name) == 0)
//			{
//				g_print("find db %s\n", db_name);
//				break;
//			}
//		}
//		mysql_free_result(res);
//	}
//	if (!row) /* Build database if no this database */
//	{
//		char buf[128] = { 0 };
//		strcpy(buf, "CREATE DATABASE ");
//		strcat(buf, db_name);
//		if (mysql_query(mysql, buf))
//		{
//			g_print("Query failed (%s)\n", mysql_error(mysql));
//			exit(1);
//		}
//	}
//	return 0;
//}
//
//gint check_tbl(MYSQL* mysql, gchar *name)
//{
//	if (name == NULL)
//		return 0;
//	MYSQL_ROW row = NULL;
//	MYSQL_RES *res = NULL;
//	res = mysql_list_tables(mysql, NULL);
//	if (res)
//	{
//		while ((row = mysql_fetch_row(res)) != NULL)
//		{
//			g_print("tables is %s\n", row[0]);
//			if (strcmp(row[0], name) == 0)
//			{
//				g_print("find the table !\n");
//				break;
//			}
//		}
//		mysql_free_result(res);
//	}
//	if (!row) /* Create table if no this table */
//	{
//		char buf[1024] = { 0 };
//		char qbuf[1024] = { 0 };
//		snprintf(buf, sizeof(buf), "%s (SN INT(10) AUTO_INCREMENT NOT NULL,pulse1 INT(10),pulse2 INT(10),pulse3 INT(10),AD1 INT(10),AD2 INT(10),AD3 INT(10),AD4 INT(10),DI INT(10),PRIMARY KEY (SN));", TABLE_NAME);
//		strcpy(qbuf, "CREATE TABLE ");
//		strcat(qbuf, buf);
//		if (mysql_query(mysql, qbuf))
//		{
//			g_print("Query failed (%s)\n", mysql_error(mysql));
//			exit(1);
//		}
//	}
//	return 0;
//}
//
//void send_to_mysql(gint rcvd_mess[])
//{
//	gchar sql_insert[200];
//	MYSQL my_connection;
//	gint res;
//
//	mysql_init(&my_connection);
//	if (mysql_real_connect(&my_connection, SERVER_HOST, SERVER_USER, SERVER_PWD, DB_NAME, 0, NULL, 0))
//	{
//		sprintf(sql_insert, "INSERT INTO mytables(pulse1,pulse2,pulse3,AD1,AD2,AD3,AD4,DI) VALUES('%d','%d','%d','%d','%d','%d','%d','%d')", rcvd_mess[0], rcvd_mess[1], rcvd_mess[2], rcvd_mess[3], rcvd_mess[4], rcvd_mess[5], rcvd_mess[6], rcvd_mess[7]);
//		res = mysql_query(&my_connection, sql_insert);
//
//		if (!res)
//		{
//			//g_print("Inserted %lu rows\n", (unsigned long)mysql_affected_rows(&my_connection));
//		}
//		else
//		{
//			fprintf(stderr, "Insert error %d: %s\n", mysql_errno(&my_connection),
//				mysql_error(&my_connection));
//		}
//		mysql_close(&my_connection);
//	}
//	else
//	{
//		if (mysql_errno(&my_connection))
//		{
//			fprintf(stderr, "Connection error %d: %s\n",
//				mysql_errno(&my_connection), mysql_error(&my_connection));
//		}
//	}
//}

/***************************************************************************************
*    Function:
*    Description:  waveform presentation
***************************************************************************************/

/* Create a new surface of the appropriate size to store our waveform */
static gboolean
draw_configure_event(GtkWidget         *widget,
	GdkEventConfigure *event,
	gpointer           data)
{
	GtkAllocation allocation;
	cairo_t *cr;

	if (surface)
		cairo_surface_destroy(surface);

	gtk_widget_get_allocation(widget, &allocation);
	surface = gdk_window_create_similar_surface(gtk_widget_get_window(widget),
		CAIRO_CONTENT_COLOR,
		allocation.width,
		allocation.height);
	/* Initialize the surface to white */
	cr = cairo_create(surface);
	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_paint(cr);
	cairo_destroy(cr);

	/* We've handled the configure event, no need for further processing. */
	return TRUE;
}

/* Redraw the screen from the surface */
static gboolean
draw_callback(GtkWidget *widget,
	cairo_t   *cr,
	gpointer   data)
{
	gdouble i = 0, x = 0, y = 0;
	gint j = 0, x_o = 0;
	gchar c[32];
	gint recv[8] = { 0 };
	gdouble big_sp = 0, small_sp = 0, width = 0, height = 0;
	gdouble Blank = 25;
	gint next = 25;
	for (j = 0; j<8; j++)
	{
		recv[j] = 0;
	}

	width = gtk_widget_get_allocated_width(widget);
	height = gtk_widget_get_allocated_height(widget);

	cairo_set_source_surface(cr, surface, 0, 0);
	cairo_paint(cr);

	big_sp = (height - 2 * Blank) / 10;
	small_sp = (height - 2 * Blank) / top_y;

	if (num >= 1) /* Calculate the maximum of current data */
	{
		recv[0] = datas[num - 1][0];
		recv[1] = datas[num - 1][1];
		recv[2] = datas[num - 1][2];
		if (recv[0]>recv[1])
		{
			if (recv[2]>recv[0]) biggest = recv[2];
			else biggest = recv[0];
		}
		else
		{
			if (recv[2]>recv[1]) biggest = recv[2];
			else biggest = recv[1];
		}
	}
	else biggest = 50;
	if (biggest >= top_y) /*Adjust the space of axis */
	{
		top_y = biggest / 50 * 50 + 50;
		big_sp = (height - 2 * Blank) / 10;
		small_sp = (height - 2 * Blank) / top_y;
	}

	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_set_line_width(cr, 0.5);
	cairo_rectangle(cr, Blank, Blank, width - 2 * Blank, height - 2 * Blank);/* Draw outer border */

	for (i = height - Blank; i>Blank - 1; i = i - big_sp)/* Draw Y-axis */
	{
		cairo_move_to(cr, Blank - 6, i);
		cairo_line_to(cr, width - Blank, i);
		cairo_move_to(cr, Blank - 25, i);
		cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
		cairo_set_font_size(cr, 12.0);
		sprintf(c, "%.0lf", y);
		y = y + top_y / 10;
		cairo_show_text(cr, c);
	}
	for (i = height - Blank; i>Blank; i = i - small_sp)
	{
		cairo_move_to(cr, Blank - 3, i);
		cairo_line_to(cr, Blank, i);
	}
	if (num>700)
	{
		x = ((num - 700) / 100 + 1) * 100;
	}
	for (i = Blank; i <= (width - Blank); i = i + 100)/* Draw X-axis */
	{
		cairo_move_to(cr, i, Blank);
		cairo_line_to(cr, i, height - Blank + 6);
		cairo_move_to(cr, i - 10, height - Blank + 16);
		cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
		cairo_set_font_size(cr, 12.0);
		sprintf(c, "%.0lf", x);
		x = x + 100;
		cairo_show_text(cr, c);
	}
	for (i = Blank; i<(width - Blank); i = i + 10)
	{
		cairo_move_to(cr, i, height - Blank);
		cairo_line_to(cr, i, height - Blank + 3);
	}
	cairo_stroke(cr);

	if (datas[0] != NULL)
	{
		for (j = 0; j<8; j++)
		{
			last_point[j] = 0;
		}

		if (num>700)/* X-axis starting value adjustment */
		{
			x_o = ((num - 700) / 100 + 1) * 100;
		}
		else x_o = 0;

		next = 24;
		for (j = x_o; j<num; j++)
		{
			cairo_set_source_rgb(cr, 0, 1, 0);/* Draw green line pulse1 */
			cairo_set_line_width(cr, 1.5);
			recv[0] = datas[j][0];
			cairo_move_to(cr, next, height - Blank - last_point[0] * small_sp);
			next++;
			cairo_line_to(cr, next, height - Blank - recv[0] * small_sp);
			last_point[0] = recv[0];
			cairo_stroke(cr);

			next--;
			cairo_set_source_rgb(cr, 1, 0, 0);/* Draw red line pulse2 */
			cairo_set_line_width(cr, 1.5);
			recv[1] = datas[j][1];
			cairo_move_to(cr, next, height - Blank - last_point[1] * small_sp);
			next++;
			cairo_line_to(cr, next, height - Blank - recv[1] * small_sp);
			last_point[1] = recv[1];
			cairo_stroke(cr);

			next--;
			cairo_set_source_rgb(cr, 0, 0, 1);/* Draw blue line pulse3 */
			cairo_set_line_width(cr, 1.5);
			recv[2] = datas[j][2];
			cairo_move_to(cr, next, height - Blank - last_point[2] * small_sp);
			next++;
			cairo_line_to(cr, next, height - Blank - recv[2] * small_sp);
			last_point[2] = recv[2];
			cairo_stroke(cr);
		}
		next--;
	}

	//cairo_set_line_width (cr, 5);
	//cairo_set_source_rgb(cr, 0, 0, 0);	
	//cairo_rectangle (cr, 0, 0, width, height);//5, 12
	//cairo_stroke (cr);
	return FALSE;
}

gboolean time_handler(GtkWidget *widget)
{
	gdouble width, height;
	width = gtk_widget_get_allocated_width(widget);
	height = gtk_widget_get_allocated_height(widget);
	if (surface2 == NULL) return FALSE;
	//if (!timer) return FALSE;
	gtk_widget_queue_draw_area(widget, 0, 0, width, height);
	return TRUE;

}

/***************************************************************************************
*    Function:
*    Description:  Pointer instrument
***************************************************************************************/
/* Create a new surface2 of the appropriate size to store our Pointer instrument */
static gboolean
draw_configure_event2(GtkWidget         *widget,
	GdkEventConfigure *event,
	gpointer           data)
{
	GtkAllocation allocation;
	cairo_t *cr;

	if (surface2)
		cairo_surface_destroy(surface2);

	gtk_widget_get_allocation(widget, &allocation);
	surface2 = gdk_window_create_similar_surface(gtk_widget_get_window(widget),
		CAIRO_CONTENT_COLOR,
		allocation.width,
		allocation.height);
	/* Initialize the surface to white */
	cr = cairo_create(surface2);
	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_paint(cr);
	cairo_destroy(cr);

	/* We've handled the configure event, no need for further processing. */
	return TRUE;
}

/* Redraw the screen from the surface */
static gboolean
draw_callback2(GtkWidget *widget,
	cairo_t   *cr,
	gpointer   data)
{
	gint i ;
	gchar c[32];
	gdouble width, height;
	width = gtk_widget_get_allocated_width(widget);
	height = gtk_widget_get_allocated_height(widget);

	cairo_set_source_surface(cr, surface2, 0, 0);
	cairo_set_source_rgb(cr, 0, 0, 0);

	double xc = width / 2;
	double yc = height / 2;
	double radius = width / 2 - 10;
	double angle1 = 0 * (M_PI / 180.0);  /* angles are specified */
	double angle2 = 360.0 * (M_PI / 180.0);  /* in radians           */

	cairo_set_line_width(cr, 5.0);
	cairo_arc(cr, xc, yc, radius, angle1, angle2);
	cairo_stroke(cr);

	/* draw helping lines */
	cairo_set_source_rgba(cr, 0, 0, 0.6, 0.6);
	cairo_set_line_width(cr, 4.0);

	cairo_arc(cr, xc, yc, 10.0, 0, 2 * M_PI);
	cairo_fill(cr);

	for (i = 0; i < 5; i++)//???
	{
		cairo_arc(cr, xc, yc, radius, (72 * i - 90) * (M_PI / 180.0), (72 * i - 90)  * (M_PI / 180.0));
		cairo_arc(cr, xc, yc, radius - 20, (72 * i - 90)  * (M_PI / 180.0), (72 * i - 90)  * (M_PI / 180.0));
		cairo_stroke(cr);
	}

	cairo_set_line_width(cr, 1.0);//???
	for (i = 0; i < 10; i++)
	{
		cairo_arc(cr, xc, yc, radius, (36 * i - 90) * (M_PI / 180.0), (36 * i - 90)  * (M_PI / 180.0));
		cairo_arc(cr, xc, yc, radius - 20, (36 * i - 90)  * (M_PI / 180.0), (36 * i - 90)  * (M_PI / 180.0));
		cairo_stroke(cr);
	}

	cairo_set_line_width(cr, 1.0);//???
	for (i = 0; i < 50; i++)
	{
		cairo_arc(cr, xc, yc, radius, (7.2 * i - 90) * (M_PI / 180.0), (7.2 * i - 90)  * (M_PI / 180.0));
		cairo_arc(cr, xc, yc, radius - 10, (7.2 * i - 90)  * (M_PI / 180.0), (7.2 * i - 90)  * (M_PI / 180.0));
		cairo_stroke(cr);
	}

	for (i = 0; i < 10; i++)//??
	{
		cairo_set_font_size(cr, 15.0);
		cairo_set_source_rgba(cr, 0, 0, 0, 1);
		gint y = 60 * i;
		sprintf_s(c, "%d", y);
		cairo_arc(cr, xc - 8, yc + 8, radius - 35, (36 * i - 90)  * (M_PI / 180.0), (36 * i - 90)  * (M_PI / 180.0));
		cairo_show_text(cr, c);
		cairo_stroke(cr);
	}

	cairo_set_font_size(cr, 15.0);
	cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_source_rgba(cr, 0, 0, 0, 1);
	cairo_move_to(cr, xc-30, yc + 30);
	//const gchar* y="试验力(kN)";
	cairo_show_text(cr, _("试验力(kN)"));	
	//cairo_show_text(cr, g_convert("试验力(kN)",-1,"UTF-8","GB2312",NULL,NULL,NULL));
	cairo_stroke(cr);

	cairo_set_source_rgb(cr, 1, 0, 0);
	cairo_set_line_width(cr, 6.0);
	cairo_arc(cr, xc, yc, radius - 30, (179 + arc_i)* (M_PI / 180.0), (180 + arc_i) * (M_PI / 180.0));
	cairo_line_to(cr, xc, yc);
	cairo_stroke(cr);
	arc_i = arc_i + 10;
	if (arc_i == 360) arc_i = 0;

	cairo_set_line_width (cr, 5);
	cairo_set_source_rgb(cr, 0.7, 0.7, 0.7);	
	cairo_rectangle (cr, 0, 0, width, height);
	cairo_stroke (cr);

	return FALSE;

}

gboolean time_handler2(GtkWidget *widget)
{
	gdouble width, height;
	width = gtk_widget_get_allocated_width(widget);
	height = gtk_widget_get_allocated_height(widget);
	if (surface2 == NULL) return FALSE;
	//if (!timer) return FALSE;
	gtk_widget_queue_draw_area(widget, 0, 0, width, height);
	return TRUE;
}

/***************************************************************************************
*    Function:
*    Description:  number presentation
***************************************************************************************/

/* Create a new surface of the appropriate size to store our waveform */
static gboolean
draw_configure_event3(GtkWidget         *widget,
	GdkEventConfigure *event,
	gpointer           data)
{
	GtkAllocation allocation;
	cairo_t *cr;

	if (surface3)
		cairo_surface_destroy(surface3);

	gtk_widget_get_allocation(widget, &allocation);
	surface3 = gdk_window_create_similar_surface(gtk_widget_get_window(widget),
		CAIRO_CONTENT_COLOR,
		allocation.width,
		allocation.height);
	/* Initialize the surface to white */
	cr = cairo_create(surface3);
	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_paint(cr);
	cairo_destroy(cr);

	/* We've handled the configure event, no need for further processing. */
	return TRUE;
}

/* Redraw the screen from the surface */
static gboolean
draw_callback3(GtkWidget *widget,
	cairo_t   *cr,
	gpointer   data)
{
	gdouble width, height;
	gchar c[8];
	width = gtk_widget_get_allocated_width(widget);
	height = gtk_widget_get_allocated_height(widget);
	cairo_set_line_width (cr, 5);
	cairo_set_source_rgb(cr, 0, 0, 0);	//black
	cairo_rectangle (cr, 0, 0, width/4, height);//5, 12
	cairo_fill_preserve (cr);
	cairo_rectangle (cr, width/4, 0, width/4, height);//290,12
	cairo_fill_preserve (cr);
	cairo_rectangle (cr, width/2, 0, width/4, height);//575,12
	cairo_fill_preserve (cr);
	cairo_rectangle (cr, width/4*3, 0, width/4, height);
	cairo_fill_preserve (cr);
	cairo_stroke (cr);
	
	cairo_set_source_rgb(cr, 0.2, 1, 1);
	cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL,CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size (cr, 50.0);
	cairo_move_to (cr, width/4-150, height-30);
	cairo_show_text (cr, "99.99");
	cairo_move_to (cr, width/4*2-150, height-30);
	cairo_show_text (cr, "99.99");
	cairo_move_to (cr, width/4*3-150, height-30);
	sprintf(c,"%.1lf",time_second);
	cairo_show_text (cr, c);
	cairo_move_to (cr, width-150, height-30);
	cairo_show_text (cr, "99.99");
	cairo_stroke (cr);

	cairo_set_source_rgb(cr, 1, 1, 1);//white
	cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL,CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size (cr, 20.0);
	cairo_move_to (cr, 0+5, height-75);
	cairo_show_text (cr, _("试验力(kN)"));
	cairo_move_to (cr, width/4+5, height-75);
	cairo_show_text (cr, _("变形(mm)"));
	cairo_move_to (cr, width/2+5, height-75);
	cairo_show_text (cr, _("时间(s)"));
	cairo_move_to (cr, width/4*3+5, height-75);
	cairo_show_text (cr, _("位移(mm)"));	
	cairo_stroke (cr);

	cairo_set_line_width (cr, 5);
	cairo_set_source_rgb(cr, 0.7, 0.7, 0.7);	
	cairo_rectangle (cr, 0, 0, width/4, height);//5, 12
	cairo_rectangle (cr, width/4, 0, width/4, height);//290,12
	cairo_rectangle (cr, width/2, 0, width/4, height);//575,12
	cairo_rectangle (cr, width/4*3, 0, width/4, height);
	cairo_stroke (cr);

	return FALSE;
}

gboolean time_handler3(GtkWidget *widget)
{
	gdouble width, height;
	width = gtk_widget_get_allocated_width(widget);
	height = gtk_widget_get_allocated_height(widget);
	if (surface3 == NULL) return FALSE;
	gtk_widget_queue_draw_area(widget, 0, 0, width, height);

	if(start==true)
	{
		time_second=time_second+0.1;
	}
	//time_second=time_second+0.1;	
	return TRUE;
}



/***************************************************************************************
*    Function:
*    Description:  textview presentation
***************************************************************************************/

void show_err(gchar *err)
{
	GtkTextIter start, end;
	gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(show_buffer), &start, &end);
	gtk_text_buffer_insert(GTK_TEXT_BUFFER(show_buffer), &end, err, strlen(err));
}

/* show the received message */
void show_remote_text(gchar rcvd_mess[])
{
	GtkTextIter start, end;
	gchar * escape, *text;
	escape = g_strescape(rcvd_mess, NULL);
	text = g_strconcat(escape, "\n", NULL);
	gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(show_buffer), &start, &end);/* Retrieves the first and last iterators in the buffer */
	gtk_text_buffer_insert(GTK_TEXT_BUFFER(show_buffer), &end, text, strlen(text));
	g_free(escape);
	g_free(text);
}

/* show the input text */
void show_local_text(const gchar* text)
{
	GtkTextIter start, end;
	gchar * escape, *text1;
	escape = g_strescape(text, NULL);
	text1 = g_strconcat(escape, "\n", NULL);
	gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(show_buffer), &start, &end);/* Retrieves the first and last iterators in the buffer */
	gtk_text_buffer_insert(GTK_TEXT_BUFFER(show_buffer), &end, text1, strlen(text1));
	g_free(escape);
	g_free(text1);
}

/***************************************************************************************
*    Function:
*    Description:  average filter function
***************************************************************************************/

void average()
{
	if (buffer1_ready == false) 
	{
		buffer1_ready = true;
	}
	if (buffer2_ready == false) 
	{
		buffer2_ready = true;
	}
}


/***************************************************************************************
*    Function:
*    Description:  socket function
***************************************************************************************/
void on_ip_button1_clicked(GtkButton *button, gpointer user_data)
{
	struct EntryStruct *entry;
	entry = (struct EntryStruct *)user_data;
	se_ip = (gchar *)gtk_entry_get_text(GTK_ENTRY(entry->IP));
	se_port = (gchar *)gtk_entry_get_text(GTK_ENTRY(entry->Port));
	//gtk_widget_destory(window);
}

/* Create ip set window */
GtkWidget *create_ip_menu_window()
{
	GtkWidget *ip_menu_window;
	GtkWidget *fixed;
	GtkWidget *close_button;
	GtkWidget *label1, *label2;
	//struct EntryStruct entries;

	label1 = gtk_label_new("IP:");
	label2 = gtk_label_new("Port:");
	entries.IP = (GtkEntry *)gtk_entry_new();
	entries.Port = (GtkEntry *)gtk_entry_new();

	ip_menu_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(ip_menu_window), "Window For IP Set");
	gtk_window_set_default_size(GTK_WINDOW(ip_menu_window), 300, 180);
	fixed = gtk_fixed_new();

	close_button = gtk_button_new_with_label(_("确定"));

	gtk_fixed_put(GTK_FIXED(fixed), label1, 40, 30);
	gtk_fixed_put(GTK_FIXED(fixed), GTK_WIDGET(entries.IP), 80, 30);
	gtk_fixed_put(GTK_FIXED(fixed), label2, 40, 80);
	gtk_fixed_put(GTK_FIXED(fixed), GTK_WIDGET(entries.Port), 80, 80);
	gtk_fixed_put(GTK_FIXED(fixed), close_button, 80, 130);

	gtk_widget_set_size_request(label1, 50, 30);
	gtk_widget_set_size_request(GTK_WIDGET(entries.IP), 100, 30);
	gtk_widget_set_size_request(label2, 50, 30);
	gtk_widget_set_size_request(GTK_WIDGET(entries.Port), 100, 30);
	gtk_widget_set_size_request(close_button, 100, 30);;

	gtk_container_add(GTK_CONTAINER(ip_menu_window), fixed);
    g_signal_connect(G_OBJECT(close_button), "clicked", G_CALLBACK(on_ip_button1_clicked),(gpointer) &entries);
	return ip_menu_window;
}

/* set ip address function */
void on_ip_menu_activate(GtkMenuItem* item, gpointer data)
{
	ip_menu_window = create_ip_menu_window();
	gtk_widget_show_all(ip_menu_window);
}

/* A new thread,to receive message */
gpointer recv_func(gpointer arg)
{
	gint i = 0;
	gchar *bufferIn=NULL;
	gint n = 0;
	socket_cache *cache=NULL;
	gint len = 45;
	GError *error = NULL;
	while (1)
	{
		if (g_socket_receive(sock, bufferIn, 512, NULL, &error)<0)
		{
			perror("server recv error\n");
			exit(1);
		}
		n = socket_msg_cpy_in(cache, (guchar *)bufferIn, len);
		if (n == 0) {
			return FALSE;//cache is full	
		}
		//parse and handle socket message from cache
		socket_msg_parse(1, cache);

		//if (n == len) {
		//	return TRUE; //copy completed
		//}
		//move the pointer
		bufferIn = bufferIn + n;
		len = len - n;
	}
}

/* Send function */
void send_func()//(const gchar *text)
{
	gchar a=0x02;
	gint n;
	GError *err = NULL;
	n = g_socket_send(sock,
		&a,
		1,
		NULL,
		&err);
	if (n<0)
	{
		perror("S send error\n");
		exit(1);
	}
}

/* Build socket connection */
gint build_socket(const gchar *serv_ip, const gchar *serv_port)
{
	gboolean res;
	g_type_init();
	GInetAddress *iface_address = g_inet_address_new_from_string(serv_ip);
	GSocketAddress *connect_address = g_inet_socket_address_new(iface_address, atoi(serv_port));
	GError *err = NULL;
	sock = g_socket_new(G_SOCKET_FAMILY_IPV4,
		G_SOCKET_TYPE_STREAM,
		G_SOCKET_PROTOCOL_TCP,
		&err);
	g_assert(err == NULL);
	res = g_socket_connect(sock,
		connect_address,
		NULL,
		&err);
	if (res == (gboolean)true)
	{
		g_thread_new(NULL, recv_func, sock); /* A new thread,to receive message */
		g_print("recv_func start...\n");
		return 0;
	}
	else
	{
		g_print("g_socket_connect error\n");
		return 1;
	}
}

/* Get the input text,and send it */
void on_send_button_clicked(GtkButton *button, gpointer user_data)
{
	gchar *text =NULL;
	struct EntryStruct1 *entry1 = (struct EntryStruct1 *)user_data;
	const gchar *DA1 = gtk_entry_get_text(GTK_ENTRY(entry1->DA1));
	const gchar *DA2 = gtk_entry_get_text(GTK_ENTRY(entry1->DA2));
	const gchar *D0 = gtk_entry_get_text(GTK_ENTRY(entry1->D0));
	const gchar *PWM = gtk_entry_get_text(GTK_ENTRY(entry1->PWM));
	const gchar *PWM_Duty = gtk_entry_get_text(GTK_ENTRY(entry1->PWM_Duty));
	const gchar *PWM_DIR = gtk_entry_get_text(GTK_ENTRY(entry1->PWM_DIR));
	if (issucceed == -1) { /* Haven't create a socket */
		show_err("Not connected...\n");
	}
	else
	{ /* Socket creating has sucMceed ,so send message */
		//text = (gchar *)malloc(MAXSIZE);
		//if (text == NULL)
		//{
		//	printf("Malloc error!\n");
		//	exit(1);
		//}
		/* get text */
		text = g_strjoin(DA1, DA2, D0, PWM, PWM_Duty, PWM_DIR, NULL);
		/* If there is no input,do nothing but return */
		if (strcmp(text, "") != 0)
		{
			send_func();
			//on_cls_button_clicked();
			show_local_text(text);
		}
		else
			show_err("The message can not be empty...\n");
		//free(text);
	}
}

/* Connect button function */
void on_button1_clicked(GtkButton *button, gpointer user_data)
{
	gint res;
	//struct EntryStruct *entry = (struct EntryStruct *)user_data;
	//const gchar *serv_ip = gtk_entry_get_text(GTK_ENTRY(entries->IP));
	//const gchar *serv_port = gtk_entry_get_text(GTK_ENTRY(entries->Port));
	//const gchar *serv_ip = (const char*)se_ip;
	//const gchar *serv_port = (const char*)se_port;
	//strcpy(serv_ip,se_ip);
	//strcpy(serv_port,se_port);

	g_print("IP: %s\n", se_ip);
	g_print("Port: %s\n", se_port);
	res = build_socket(se_ip, se_port);
	if (res == 1)
		g_print("IP Address is  Invalid...\n");
	else if (res == -1)
		g_print("Connect Failure... \n");
	else
	{
		//init_db();
		g_print("Connect Successful... \n");
		issucceed = 0;
	}
	start=true;
}

/***************************************************************************************
*    Function:
*    Description:  create report
***************************************************************************************/

static void check_max(GtkWidget *max, gpointer user_data)
{
	has_max = TRUE;
}

static void check_min(GtkWidget *min, gpointer user_data)
{
	has_min = TRUE;
}

static void check_run_time(GtkWidget *run_time, gpointer user_data)
{
	has_run_time = TRUE;
}

static void check_date_time(GtkWidget *date_time, gpointer user_data)
{
	has_date_time = TRUE;
}

/* Report button function */
void on_report_button_clicked(GtkButton *button, gpointer user_data)
{
	cairo_surface_t *report_surface;
	cairo_t *cr;
	gdouble i = 0, x = 0, y = 0, Blank = 25, next = 25;
	gdouble big_sp, small_sp, width, height, tr_down, tr_right;
	gint j = 0, x_o;
	gchar c[4];
	gint recv[8];

	report_surface = cairo_pdf_surface_create("HelloWorld.pdf", 595.28, 765.35);
	cr = cairo_create(report_surface);
	cairo_set_source_surface(cr, surface, 0, 0);
	cairo_paint(cr);

	width = 400;
	height = 300;

	for (j = 0; j<8; j++)
	{
		recv[j] = 0;
	}

	big_sp = (height - 2 * Blank) / 10;
	small_sp = (height - 2 * Blank) / top_y;

	if (num >= 1) /* Calculate the maximum of current data */
	{
		recv[0] = datas[num - 1][0];
		recv[1] = datas[num - 1][1];
		recv[2] = datas[num - 1][2];
		if (recv[0]>recv[1])
		{
			if (recv[2]>recv[0]) biggest = recv[2];
			else biggest = recv[0];
		}
		else
		{
			if (recv[2]>recv[1]) biggest = recv[2];
			else biggest = recv[1];
		}
	}
	else biggest = 50;
	if (biggest >= top_y) /*Adjust the space of axis */
	{
		top_y = biggest / 50 * 50 + 50;
		big_sp = (height - 2 * Blank) / 10;
		small_sp = (height - 2 * Blank) / top_y;
	}

	cairo_move_to(cr, 230, 50);
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_select_font_face(cr, "Georgia", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(cr, 20);
	cairo_show_text(cr, "Testing Report");
	cairo_stroke(cr);

	tr_down = 80;
	tr_right = 100;

	if ((gboolean)true == has_max)
	{
		tr_down = tr_down + 30;
		cairo_move_to(cr, 40, tr_down);
		cairo_set_source_rgb(cr, 0, 0, 0);
		cairo_select_font_face(cr, "Georgia", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
		cairo_set_font_size(cr, 12);
		cairo_show_text(cr, "max value:");
		cairo_stroke(cr);
	}
	if ((gboolean)true == has_min)
	{
		tr_down = tr_down + 30;
		cairo_move_to(cr, 40, tr_down);
		cairo_set_source_rgb(cr, 0, 0, 0);
		cairo_select_font_face(cr, "Georgia", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
		cairo_set_font_size(cr, 12);
		cairo_show_text(cr, "min value:");
		cairo_stroke(cr);
	}
	if ((gboolean)true == has_date_time)
	{
		tr_down = tr_down + 30;
		cairo_move_to(cr, 40, tr_down);
		cairo_set_source_rgb(cr, 0, 0, 0);
		cairo_select_font_face(cr, "Georgia", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
		cairo_set_font_size(cr, 12);
		cairo_show_text(cr, "date time:");
		cairo_stroke(cr);
	}
	if ((gboolean)true == has_run_time)
	{
		tr_down = tr_down + 30;
		cairo_move_to(cr, 40, tr_down);
		cairo_set_source_rgb(cr, 0, 0, 0);
		cairo_select_font_face(cr, "Georgia", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
		cairo_set_font_size(cr, 12);
		cairo_show_text(cr, "running time:");
		cairo_stroke(cr);
	}

	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_set_line_width(cr, 0.5);
	cairo_rectangle(cr, Blank + tr_right, tr_down + Blank, width - 2 * Blank, height - 2 * Blank);/* Draw outer border */

	for (i = tr_down + height - Blank; i>tr_down + Blank - 1; i = i - big_sp)/* Draw Y-axis */
	{
		cairo_move_to(cr, Blank + tr_right - 6, i);
		cairo_line_to(cr, width + tr_right - Blank, i);
		cairo_move_to(cr, Blank + tr_right - 20, i);
		cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
		cairo_set_font_size(cr, 12.0);
		sprintf(c, "%.0lf", y);
		y = y + top_y / 10;
		cairo_show_text(cr, c);
	}
	for (i = tr_down + height - Blank; i>tr_down + Blank; i = i - small_sp)
	{
		cairo_move_to(cr, Blank + tr_right - 3, i);
		cairo_line_to(cr, Blank + tr_right, i);
	}
	if (num>700)
	{
		x = ((num - 700) / 100 + 1) * 100;
	}
	for (i = Blank + tr_right; i <= (width + tr_right - Blank); i = i + 50)/* Draw X-axis */
	{
		cairo_move_to(cr, i, tr_down + Blank);
		cairo_line_to(cr, i, tr_down + height - Blank + 6);
		cairo_move_to(cr, i, tr_down + height - Blank + 16);
		cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
		cairo_set_font_size(cr, 12.0);
		sprintf(c, "%.0lf", x);
		x = x + 100;
		cairo_show_text(cr, c);
	}
	for (i = Blank + tr_right; i<(width + tr_right - Blank); i = i + 5)
	{
		cairo_move_to(cr, i, tr_down + height - Blank);
		cairo_line_to(cr, i, tr_down + height - Blank + 3);
	}
	cairo_stroke(cr);

	if (datas[0] != NULL)
	{
		for (j = 0; j<8; j++)
		{
			last_point[j] = 0;
		}

		if (num>700)/* X-axis starting value adjustment */
		{
			x_o = ((num - 700) / 100 + 1) * 100;
		}
		else x_o = 0;

		next = 48;
		for (j = x_o; j<num; j++)
		{
			cairo_set_source_rgb(cr, 0, 1, 0);/* Draw green line pulse1 */
			cairo_set_line_width(cr, 1.2);
			recv[0] = datas[j][0];
			cairo_move_to(cr, next / 2 + tr_right, tr_down + height - Blank - last_point[0] * small_sp);
			next++;
			cairo_line_to(cr, next / 2 + tr_right, tr_down + height - Blank - recv[0] * small_sp);
			last_point[0] = recv[0];
			cairo_stroke(cr);

			next--;
			cairo_set_source_rgb(cr, 1, 0, 0);/* Draw red line pulse2 */
			cairo_set_line_width(cr, 1.2);
			recv[1] = datas[j][1];
			cairo_move_to(cr, next / 2 + tr_right, tr_down + height - Blank - last_point[1] * small_sp);
			next++;
			cairo_line_to(cr, next / 2 + tr_right, tr_down + height - Blank - recv[1] * small_sp);
			last_point[1] = recv[1];
			cairo_stroke(cr);

			next--;
			cairo_set_source_rgb(cr, 0, 0, 1);/* Draw blue line pulse3 */
			cairo_set_line_width(cr, 1.2);
			recv[2] = datas[j][2];
			cairo_move_to(cr, next / 2 + tr_right, tr_down + height - Blank - last_point[2] * small_sp);
			next++;
			cairo_line_to(cr, next / 2 + tr_right, tr_down + height - Blank - recv[2] * small_sp);
			last_point[2] = recv[2];
			cairo_stroke(cr);
		}
		next--;
	}
	cairo_show_page(cr);
	cairo_surface_destroy(report_surface);
	cairo_destroy(cr);
}

/* Create report window */
GtkWidget *create_report_window()
{
	GtkWidget *report_window;
	GtkWidget *fixed;
	GtkWidget *report_button;
	GtkWidget *max, *min, *run_time, *date_time, *name;
	GtkWidget *name_label;

	report_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	name = gtk_entry_new();
	name_label = gtk_label_new("Note Taker:");
	gtk_window_set_title(GTK_WINDOW(report_window), "Window For Report");
	gtk_window_set_default_size(GTK_WINDOW(report_window), 300, 450);
	fixed = gtk_fixed_new();

	max = gtk_check_button_new_with_label("Record maximum");
	min = gtk_check_button_new_with_label("Record minimum");
	run_time = gtk_check_button_new_with_label("Record running time");
	date_time = gtk_check_button_new_with_label("Record running date");
	report_button = gtk_button_new_with_label("Create report");
	gtk_fixed_put(GTK_FIXED(fixed), name_label, 20, 20);
	gtk_fixed_put(GTK_FIXED(fixed), name, 100, 20);
	gtk_fixed_put(GTK_FIXED(fixed), max, 20, 50);
	gtk_fixed_put(GTK_FIXED(fixed), min, 20, 80);
	gtk_fixed_put(GTK_FIXED(fixed), run_time, 20, 110);
	gtk_fixed_put(GTK_FIXED(fixed), date_time, 20, 140);
	gtk_fixed_put(GTK_FIXED(fixed), report_button, 20, 400);

	gtk_widget_set_size_request(name, 50, 20);
	gtk_widget_set_size_request(max, 80, 20);
	gtk_widget_set_size_request(min, 80, 20);
	gtk_widget_set_size_request(run_time, 80, 20);
	gtk_widget_set_size_request(date_time, 80, 20);
	gtk_widget_set_size_request(report_button, 80, 20);

	gtk_container_add(GTK_CONTAINER(report_window), fixed);

	g_signal_connect(G_OBJECT(max), "toggled", G_CALLBACK(check_max), (gpointer)max);
	g_signal_connect(G_OBJECT(min), "toggled", G_CALLBACK(check_min), (gpointer)min);
	g_signal_connect(G_OBJECT(run_time), "toggled", G_CALLBACK(check_run_time), (gpointer)run_time);
	g_signal_connect(G_OBJECT(date_time), "toggled", G_CALLBACK(check_date_time), (gpointer)date_time);
	g_signal_connect(G_OBJECT(report_button), "clicked", G_CALLBACK(on_report_button_clicked), NULL);//(gpointer) surface);

	return report_window;
}

/* pre_report_button button function */
void on_pre_report_button_clicked(GtkButton *button, gpointer user_data)
{
	report_window = create_report_window();
	gtk_widget_show_all(report_window);
}

/***************************************************************************************
*    Function:
*    Description:
***************************************************************************************/

/* Menu key test */
void on_menu_activate(GtkMenuItem* item, gpointer data)
{
	g_print("menuitem %s is pressed.\n", (gchar*)data);
}

/* Clean the input text */
void on_cls_button_clicked()
{
	GtkTextIter start, end;
	gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(input_buffer), &start, &end);/* Retrieves the first and last iterators in the buffer */
	gtk_text_buffer_delete(GTK_TEXT_BUFFER(input_buffer), &start, &end);
}

/* Stop the GTK+ main loop function. */
static void destroy(GtkWidget *window, gpointer data)
{
	gint i;
	for (i = 0; i<360000; i++)
	{
		g_free(datas[i]);
	}
	g_free(datas);

	gtk_main_quit();
}

gint main(gint argc, char *argv[])
{
	gint i = 0;
	GtkWidget *window;
	GtkWidget *label1, *label2, *label3, *label4, *label5, *label6, *label7, *label8, *label9, *label10, *label11, *label12;
	GtkWidget *conn_button, *close_button, *send_button, *pre_report_button;
	GtkWidget *rece_view;
	GtkWidget *da;
	GtkWidget *sector;
	GtkWidget *menubar;
	GtkWidget *menu1, *menu2, *menu3, *menu4, *menu5, *menu6, *menu7;
	GtkWidget *setmenu, *adjustmenu, *toolmenu, *winmenu, *helpmenu, *ipmenu, *exitmenu;
	GtkWidget *s_force_sensor, *s_extensometer, *sys_para, *analy_para, *force_verfic, *extensometer_verfic, *dis_verific, *compress_db, *i_o_db, *lock, *Float, *auto_arrange, *array_win, *move_up_left, *about, *reg;
	GtkAccelGroup *accel_group;

	GtkWidget *grid;
	GtkWidget *scrolled1;

	GtkWidget *num;

	gtk_init(&argc, &argv);
	//struct EntryStruct entries;
	struct EntryStruct1 entries1;

	datas = (gint **)g_malloc(sizeof(gint *) * 360000);
	for (i = 0; i<360000; i++)
	{
		datas[i] = (gint *)g_malloc(sizeof(gint) * 8);
	}

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(window), "Window For Fatigue-Test (Win32)");
	gtk_container_set_border_width(GTK_CONTAINER(window), 0);
	gtk_widget_set_size_request(window, 1200, 650);

	grid = gtk_grid_new();

	label1 = gtk_label_new("IP:");
	label2 = gtk_label_new("Port:");
	label3 = gtk_label_new("DA1:");
	label4 = gtk_label_new("DA2:");
	label5 = gtk_label_new("D0:");
	label6 = gtk_label_new("PWM:");
	label7 = gtk_label_new("Duty Cycle:");
	label8 = gtk_label_new("PWM-DIR:");
	label9 = gtk_label_new("Messages:");
	label10 = gtk_label_new(_("试验力-时间曲线"));
	label11 = gtk_label_new(_("试验力(kN)"));
	//gtk_widget_set_size_request(label11,1,10);/*设置标号尺寸*/
	gtk_label_set_justify(GTK_LABEL(label11),GTK_JUSTIFY_CENTER);/*设置标号对齐方式为居中对齐*/
	gtk_label_set_line_wrap(GTK_LABEL(label11),TRUE);/*打开自动换行*/
	label12 = gtk_label_new(_("时间(s)"));
	//entries.IP = (GtkEntry*)gtk_entry_new();
	//entries.Port = (GtkEntry*)gtk_entry_new();
	entries1.DA1 = (GtkEntry*)gtk_entry_new();
	entries1.DA2 = (GtkEntry*)gtk_entry_new();
	entries1.D0 = (GtkEntry*)gtk_entry_new();
	entries1.PWM = (GtkEntry*)gtk_entry_new();
	entries1.PWM_Duty = (GtkEntry*)gtk_entry_new();
	entries1.PWM_DIR = (GtkEntry*)gtk_entry_new();
	rece_view = gtk_text_view_new();
	da = gtk_drawing_area_new();
	sector = gtk_drawing_area_new();
	num = gtk_drawing_area_new();
	accel_group = gtk_accel_group_new();

	//gtk_entry_set_text(GTK_ENTRY(entries.IP), "111.186.100.57");
	//gtk_entry_set_text(GTK_ENTRY(entries.Port), "8888");
	gtk_entry_set_text(GTK_ENTRY(entries1.DA1), "0");
	gtk_entry_set_text(GTK_ENTRY(entries1.DA2), "0");
	gtk_entry_set_text(GTK_ENTRY(entries1.D0), "0");
	gtk_entry_set_text(GTK_ENTRY(entries1.PWM), "0");
	gtk_entry_set_text(GTK_ENTRY(entries1.PWM_Duty), "0");
	gtk_entry_set_text(GTK_ENTRY(entries1.PWM_DIR), "0");
	gtk_entry_set_alignment(GTK_ENTRY(entries1.DA1), 1);
	gtk_entry_set_alignment(GTK_ENTRY(entries1.DA2), 1);
	gtk_entry_set_alignment(GTK_ENTRY(entries1.D0), 1);
	gtk_entry_set_alignment(GTK_ENTRY(entries1.PWM), 1);
	gtk_entry_set_alignment(GTK_ENTRY(entries1.PWM_Duty), 1);
	gtk_entry_set_alignment(GTK_ENTRY(entries1.PWM_DIR), 1);

	g_timeout_add(10, (GSourceFunc)time_handler, (gpointer)da);
	g_timeout_add(100, (GSourceFunc)time_handler2, (gpointer)sector);
	g_timeout_add(100, (GSourceFunc)time_handler3, (gpointer)num);

	/* Get the buffer of textbox */
	show_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(rece_view));
	/* Set textbox to diseditable */
	gtk_text_view_set_editable(GTK_TEXT_VIEW(rece_view), FALSE);
	/* Scroll window */
	scrolled1 = gtk_scrolled_window_new(NULL, NULL);
	/* Create a textbox */
	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrolled1), rece_view);
	/* Setting of window */
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled1), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	g_signal_connect(G_OBJECT(da), "draw", G_CALLBACK(draw_callback), NULL);
	g_signal_connect(G_OBJECT(da), "configure-event", G_CALLBACK(draw_configure_event), NULL);
	g_signal_connect(G_OBJECT(sector), "draw", G_CALLBACK(draw_callback2), NULL);
	g_signal_connect(G_OBJECT(sector), "configure-event", G_CALLBACK(draw_configure_event2), NULL);
	g_signal_connect(G_OBJECT(num), "draw", G_CALLBACK(draw_callback3), NULL);
	g_signal_connect(G_OBJECT(num), "configure-event", G_CALLBACK(draw_configure_event3), NULL);

	/* Create a new button that send messages */
	send_button = gtk_button_new_with_label("Send");
	g_signal_connect(G_OBJECT(send_button), "clicked", G_CALLBACK(on_send_button_clicked), (gpointer)&entries1);
	conn_button = gtk_button_new_with_label(_("开始"));
	//gtk_button_set_relief(GTK_BUTTON(conn_button), GTK_RELIEF_NONE);
	g_signal_connect(G_OBJECT(conn_button), "clicked", G_CALLBACK(on_button1_clicked), NULL);//(gpointer)&entries);

	/* Create a new button that has a mnemonic key of Alt+C. */
	close_button = gtk_button_new_with_mnemonic("Close");
	//gtk_button_set_relief(GTK_BUTTON(close_button), GTK_RELIEF_NONE);
	g_signal_connect_swapped(G_OBJECT(close_button), "clicked", G_CALLBACK(destroy), (gpointer)window);
	g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(destroy), NULL);

	/* Create a new button that prepare for the report */
	pre_report_button = gtk_button_new_with_label(_("停止"));
	g_signal_connect(G_OBJECT(pre_report_button), "clicked", G_CALLBACK(on_pre_report_button_clicked), (gpointer)surface);

	/* Create a menuitem to expand */
	menubar = gtk_menu_bar_new();
	menu1 = gtk_menu_new();
	menu2 = gtk_menu_new();
	menu3 = gtk_menu_new();
	menu4 = gtk_menu_new();
	menu5 = gtk_menu_new();
	menu6 = gtk_menu_new();
	menu7 = gtk_menu_new();

	setmenu = gtk_menu_item_new_with_label(_(" 设置 "));
	adjustmenu = gtk_menu_item_new_with_label(_(" 调整 "));
	toolmenu = gtk_menu_item_new_with_label(_(" 工具 "));
	winmenu = gtk_menu_item_new_with_label(_(" 窗口 "));
	helpmenu = gtk_menu_item_new_with_label(_(" 帮助 "));
	ipmenu = gtk_menu_item_new_with_label(_(" IP "));
	exitmenu = gtk_menu_item_new_with_label(_(" 退出 "));

	s_force_sensor= gtk_menu_item_new_with_label(_(" 选择力传感器 "));
	s_extensometer= gtk_menu_item_new_with_label(_(" 选择引伸计 "));
	sys_para= gtk_menu_item_new_with_label(_(" 系统参数 "));
	analy_para= gtk_menu_item_new_with_label(_(" 分析参数 "));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu1),s_force_sensor);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu1),s_extensometer);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu1),sys_para);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu1),analy_para);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(setmenu),menu1);

	force_verfic= gtk_menu_item_new_with_label(_(" 力传感器检定 "));
	extensometer_verfic= gtk_menu_item_new_with_label(_(" 引伸计检定 "));
	dis_verific= gtk_menu_item_new_with_label(_(" 位移检定 "));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu2),force_verfic);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu2),extensometer_verfic);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu2),dis_verific);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(adjustmenu),menu2);

	compress_db= gtk_menu_item_new_with_label(_(" 压缩数据库 "));
	i_o_db= gtk_menu_item_new_with_label(_(" 数据库导入导出 "));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu3), compress_db);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu3), i_o_db);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(toolmenu), menu3);

	lock= gtk_menu_item_new_with_label(_(" 锁定 "));
	Float= gtk_menu_item_new_with_label(_(" 浮动 "));
	auto_arrange= gtk_menu_item_new_with_label(_(" 自动排列 "));
	array_win= gtk_menu_item_new_with_label(_(" 排列子窗口 "));
	move_up_left= gtk_menu_item_new_with_label(_(" 移动到屏幕左上角 "));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu4), lock);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu4), Float);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu4), auto_arrange);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu4), array_win);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu4), move_up_left);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(winmenu), menu4);

	about= gtk_menu_item_new_with_label(_(" 关于 "));
	reg= gtk_menu_item_new_with_label(_(" 注册 "));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu5), about);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu5), reg);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(helpmenu), menu5);

	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), setmenu);
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), adjustmenu);
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), toolmenu);
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), winmenu);
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), helpmenu);
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), ipmenu);
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), exitmenu);

	g_signal_connect(G_OBJECT(ipmenu), "activate", G_CALLBACK(on_ip_menu_activate), NULL);
	g_signal_connect(G_OBJECT(exitmenu), "activate", G_CALLBACK(destroy), (gpointer)window);


	gtk_grid_attach(GTK_GRID(grid), menubar, 0, 0, 1200, 30);
	
	gtk_grid_attach(GTK_GRID(grid), num, 0, 30, 1200, 100);
	gtk_grid_attach(GTK_GRID(grid), label10, 400, 120, 50, 50);
	gtk_grid_attach(GTK_GRID(grid), label11, 0, 300, 50, 10);
	gtk_grid_attach(GTK_GRID(grid), da, 30, 160, 900-40, 500);
	gtk_grid_attach(GTK_GRID(grid), label12, 400, 650, 50, 50);
	gtk_grid_attach(GTK_GRID(grid), sector, 900, 200, 300, 300);
	gtk_grid_attach(GTK_GRID(grid), conn_button, 960, 540, 70, 70);
	gtk_grid_attach(GTK_GRID(grid), pre_report_button, 1070, 540, 70, 70);


	//gtk_grid_attach(GTK_GRID(grid), label1, 0, 50, 50, 30);
	//gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(entries.IP), 50, 50, 100, 30);
	//gtk_grid_attach(GTK_GRID(grid), label2, 150, 50, 50, 40);
	//gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(entries.Port), 200, 50, 50, 30);
	////gtk_grid_attach(GTK_GRID(grid), conn_button, 0, 100, 80, 30);
	////gtk_grid_attach(GTK_GRID(grid), close_button, 100, 100, 80, 30);
	//gtk_grid_attach(GTK_GRID(grid), conn_button, 250, 50, 80, 30);
	//gtk_grid_attach(GTK_GRID(grid), close_button, 330, 50, 80, 30);

	//gtk_grid_attach(GTK_GRID(grid), num, 5, 150, 687, 65);
	//gtk_grid_attach(GTK_GRID(grid), da, 5, 220, 687, 450);

	//gtk_grid_attach(GTK_GRID(grid), label9, 700, 150, 50, 50);
	//gtk_grid_attach(GTK_GRID(grid), scrolled1, 765, 150, 50, 50);
	//gtk_grid_attach(GTK_GRID(grid), label3, 690, 200, 80, 50);
	//gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(entries1.DA1), 765, 200, 50, 50);
	//gtk_grid_attach(GTK_GRID(grid), label4, 690, 230, 80, 50);
	//gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(entries1.DA2), 765, 230, 50, 50);
	//gtk_grid_attach(GTK_GRID(grid), label5, 690, 260, 80, 50);
	//gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(entries1.D0), 765, 260, 50, 50);
	//gtk_grid_attach(GTK_GRID(grid), label6, 690, 290, 80, 50);
	//gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(entries1.PWM), 765, 290, 50, 50);
	//gtk_grid_attach(GTK_GRID(grid), label7, 690, 320, 80, 50);
	//gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(entries1.PWM_Duty), 765, 320, 50, 50);
	//gtk_grid_attach(GTK_GRID(grid), label8, 690, 350, 80, 50);
	//gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(entries1.PWM_DIR), 765, 350, 50, 50);
	//gtk_grid_attach(GTK_GRID(grid), send_button, 765, 400, 50, 20);
	//gtk_grid_attach(GTK_GRID(grid), pre_report_button, 765, 430, 50, 20);
	//gtk_grid_attach(GTK_GRID(grid), sector, 700, 470, 115, 175);

	gtk_grid_set_row_spacing(GTK_GRID(grid), 1);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 1);
	gtk_container_add(GTK_CONTAINER(window), grid);

	gtk_widget_show_all(window);

	gtk_main();

	return 0;
}
