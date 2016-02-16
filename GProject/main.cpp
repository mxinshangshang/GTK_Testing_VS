/**************************************************************************************
*  File name:      Fatigue_Tester.c
*  Author:         Mxin Chiang
*  Version:        1.0
*  Date:           02.16.2016
*  Description:    Design a software accepts data sent from fatigue testing machine,
*                  waveform presentation, recording in MySQL database,
*                  data processing and generate pdf reports.
*  Others:
*  Function List:
***************************************************************************************/
#define _CRT_SECURE_NO_DEPRECATE
#define _WIN32_ 1    /* Compile for WIN32 */
//#define _LINUX_ 1    /* Compile for Linux */
//#define wei 1

#ifdef _WIN32_
#define WIN32_LEAN_AND_MEAN
#include <gtk\gtk.h>
#include <windows.h>
#include <winsock2.h>
#include "mysql.h"
#endif

#ifdef _LINUX_
#include <gtk/gtk.h>
#include <unistd.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <semaphore.h>
#include <mysql/mysql.h>
#endif

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <cairo-pdf.h>
#include <math.h>
#include "socket_msg.h"


#define SERVER_HOST "localhost"
#define SERVER_USER "root"
#define SERVER_PWD  "12345"

#define DB_NAME     "fatigue_test_db"
#define TABLE_NAME  "mytables"

#ifndef M_PI
#define M_PI 3.1415926
#endif
#define FILTER_N 100


//#define CAIRO_HAS_PDF_SURFACE 1

gint check_tbl(MYSQL* mysql, gchar *name);
gint check_db(MYSQL *mysql, gchar *db_name);

gdouble **datas;
gint data_num = 0;
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
gint top_x;
gint top_y;
gdouble arc_i = 0.0;

gdouble recv_temp[8] = { 0 };
gint recv_num = 0;
gint filter_buf[8][101];

#ifdef wei
gchar txt_name[20];
FILE *fp = NULL;  /* test file */
#endif

struct EntryStruct
{
	GtkEntry *IP;
	GtkEntry *Port;
	GtkEntry *batch;
	GtkEntry *num;
	GtkEntry *time;
	GtkEntry *temp;
	GtkEntry *name;
	GtkEntry *combo;
	GtkEntry *outer;
	GtkEntry *thick;
	GtkEntry *span;
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

gchar *se_ip;//= g_new(gchar, 1);
gchar *se_port;//= g_new(gchar, 1);
struct EntryStruct entries;

gdouble time_second = 0.0;
gboolean start = FALSE;

guchar *bufferIn;
socket_cache *cache;
gdouble P1 = 0.00;
gdouble P2 = 0.00;
gdouble P3 = 0.00;
gdouble AD1 = 0.00;
gdouble AD2 = 0.00;
gdouble AD3 = 0.00;
gdouble AD4 = 0.00;
gchar DI;
gdouble Fbb_data = 0.0000;
gdouble mE_k_data = 0.0000;
gdouble mE_data = 0.0000;
gdouble Rbb_data = 0.0000;
gboolean mE_k_data_ok = FALSE;

gdouble _fXBegin=0.0; //��ǰ��ʾ���ε�X����ʼ����ֵ
gdouble _fXEnd=10.0; //��ǰ��ʾ���ε�X���������ֵ
gdouble _fYBegin=0.0; //��ǰ��ʾ���ε�Y����ʼ����ֵ
gdouble _fYEnd=100.0; //��ǰ��ʾ���ε�Y���������ֵ
gdouble _fXBeginGO; //��ǰ��ʾ���ε�X������궨��ʼֵ
gdouble _fXEndGO; //��ǰ��ʾ���ε�X������궨����ֵ
gdouble _fYBeginGO; //��ǰ��ʾ���ε�Y������궨��ʼֵ
gdouble _fYEndGO; //��ǰ��ʾ���ε�Y������궨����ֵ

gdouble _fXQuanBeginGO; //��ǰ��ʾ���ε�X������궨��ʼȨֵ
gdouble _fXQuanEndGO; //��ǰ��ʾ���ε�X������궨����Ȩֵ
gdouble _fYQuanBeginGO; //��ǰ��ʾ���ε�Y������궨��ʼȨֵ
gdouble _fYQuanEndGO; //��ǰ��ʾ���ε�Y������궨����Ȩֵ

gchar *_(gchar *c)
{
	return(g_locale_to_utf8(c, -1, NULL, NULL, NULL));
}

/***************************************************************************************
*    Function:
*    Description:  socket msg parse
***************************************************************************************/
//initialize the socket_msg structure
void socket_msg_init(socket_msg *msg)
{
	msg->len = 29;
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
				(cache->buf[q] == ((SOCKET_MSG_HEAD & 0xffffff) >> 16)) &&
				(cache->buf[m] == ((SOCKET_MSG_HEAD & 0xffff) >> 8)) &&
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
			//move 4 items towards next
			cache->current = (cache->current + 4) % SOCKET_MSG_CACHE_SIZE;
			//we found the head format, go on to find Type byte
			cache->strategy = SEARCH_FIRST;
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
		cache->current = (cache->current + 3) % SOCKET_MSG_CACHE_SIZE;
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
		cache->current = (cache->current + 4) % SOCKET_MSG_CACHE_SIZE;
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
			(cache->buf[q] == ((SOCKET_MSG_END & 0xffffff) >> 16)) &&
			(cache->buf[m] == ((SOCKET_MSG_END & 0xffff) >> 8)) &&
			(cache->buf[n] == (SOCKET_MSG_END & 0xff)))
		{
			socket_msg_cpy_out(cache, cache->recv_msg.data, cache->current - SOCKET_MSG_SECOND_SIZE, cache->recv_msg.len);
			if (cache->handle != NULL) {
				//cache->handle(fd, cache->buf + cache->data_index, cache->data_len);
				cache->handle(fd, &cache->recv_msg, cache->args);
			}
			//delete all previous items
			cache->current = (n + 1) % SOCKET_MSG_CACHE_SIZE;
			cache->front = cache->current;
			cache->len -= (cache->recv_msg.len + SOCKET_MSG_FORMAT_SIZE);
			cache->tag = 0;

		}
		else {
			g_print("socket message without end: %x!\n", SOCKET_MSG_END);
			//delete the frist item '55'
			//move back 7 items
			cache->current = cache->current >= 7 ? (cache->current - 7) : (SOCKET_MSG_CACHE_SIZE - 7 + cache->current);
			cache->front = cache->current;
			//length sub 7
			cache->len -= 7;
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
	msg->data[0] = (SOCKET_MSG_HEAD >> 8);
	msg->data[1] = (SOCKET_MSG_HEAD & 0xff);
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
	gint i = 0;
	gint j = 0;
	gint data[8];
	for (i = 0; i<8; i++)
	{
		data[i] = recv_data[i * 4] << 24 + recv_data[i * 4 + 1] << 16 + recv_data[i * 4 + 2] << 8 + recv_data[i * 4 + 3];
	}
	if (recv_num >= 100)
	{
		for (i = 1; i<100; i++)
		{
			for (j = 0; j<8; j++)
			{
				filter_buf[i][j] = filter_buf[i - 1][j];
			}
		}
		for (i = 0; i<8; i++)
		{
			filter_buf[i][100] = data[i];
		}
		for (i = 0; i<8; i++)
		{
			data[i] = 0;
		}
		for (i = 0; i<8; i++)
		{
			for (j = 0; j<100; j++)
			{
				data[i] = data[i] + filter_buf[i][j];
			}
			data[i] = data[i] / 100;
		}
		return data;
		recv_num = 100;
	}
	else if (recv_num<100)
	{
		for (i = 0; i<8; i++)
		{
			filter_buf[i][recv_num] = data[i];
		}
		recv_num++;
		return data;
	}
}


/***************************************************************************************
*    Function:
*    Description:  Database Operations
***************************************************************************************/

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
//		snprintf(buf, sizeof(buf), "%s (SN INT(10) AUTO_INCREMENT NOT NULL,pulse1 DOUBLE(16,4),pulse2 DOUBLE(16,4),pulse3 DOUBLE(16,4),AD1 DOUBLE(16,4),AD2 DOUBLE(16,4),AD3 DOUBLE(16,4),AD4 DOUBLE(16,4),DI DOUBLE(16,4),PRIMARY KEY (SN));", TABLE_NAME);
//		//snprintf(buf,sizeof(buf),"%s (SN INT(10) AUTO_INCREMENT NOT NULL,pulse1 INT(10),pulse2 INT(10),pulse3 INT(10),AD1 INT(10),AD2 INT(10),AD3 INT(10),AD4 INT(10),DI INT(10),PRIMARY KEY (SN));",TABLE_NAME);
//		//		        strcpy(qbuf,"CREATE TABLE ");
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
//void send_to_mysql(gdouble rcvd_mess[])
//{
//	gchar sql_insert[200];
//	MYSQL my_connection;
//	gint res;
//
//	mysql_init(&my_connection);
//	if (mysql_real_connect(&my_connection, SERVER_HOST, SERVER_USER, SERVER_PWD, DB_NAME, 0, NULL, 0))
//	{
//		sprintf(sql_insert, "INSERT INTO mytables(pulse1,pulse2,pulse3,AD1,AD2,AD3,AD4,DI) VALUES('%.6lf','%.6lf','%.6lf','%.6lf','%.6lf','%.6lf','%.6lf','%.6lf')", rcvd_mess[0], rcvd_mess[1], rcvd_mess[2], rcvd_mess[3], rcvd_mess[4], rcvd_mess[5], rcvd_mess[6], rcvd_mess[7]);
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
*    Description: Waveform presentation scale adjustment
***************************************************************************************/
gdouble _getQuan(gdouble m)
{
	gdouble quan = 1.0f;        //��ʱ��Ȩֵ
	m = (m < 0) ? -m : m;   //ȡ����ֵ
	if (m == 0)
	{
		return 1.0f;          //Ĭ��0��ȨֵΪ1
	}
	else if (m < 1)
	{
		do { quan /= 10.0f; } while ((m = m * 10.0f) < 1);
		return quan;
	}
	else
	{
		while ((m /= 10.0f) >= 1) { quan *= 10.0f; }
		return quan;
	}
}

void _changXBegionOrEndGO(gdouble m, gboolean isL)
{
	gdouble quan = _getQuan(m);   //��ø��������Ȩֵ
	if (isL)
	{
		//���ֵ�Ǵ�������
			if (quan < _fXQuanEndGO)
			{
				_fXQuanBeginGO = _fXQuanEndGO / 10.0f;
			}
			else if (quan > _fXQuanEndGO)
			{
				_fXQuanBeginGO = quan;
				_fXQuanEndGO = _fXQuanBeginGO / 10.0f;
			}
			else
			{
				_fXQuanBeginGO = _fXQuanEndGO;
			}
			if (m <= _fXQuanBeginGO && m >= -_fXQuanBeginGO)
			{
				_fXBeginGO = -_fXQuanBeginGO;
			}
			else
			{
				_fXBeginGO = ((int)(m / _fXQuanBeginGO) - 1) * _fXQuanBeginGO;
			}
	}
	else
	{
		//���ֵ�Ǵ��ұ����
			if (quan < _fXQuanBeginGO)
			{
				_fXQuanEndGO = _fXQuanBeginGO / 10.0f;
			}
			else if (quan > _fXQuanBeginGO)
			{
				_fXQuanEndGO = quan;
				_fXQuanBeginGO = _fXQuanEndGO / 10.0f;
			}
			else
			{
				_fXQuanEndGO = _fXQuanBeginGO;
			}
			if (m <= _fXQuanEndGO && m >= _fXQuanBeginGO)
			{
				_fXEndGO = _fXQuanEndGO;
			}
			else
			{
				_fXEndGO = ((int)(m / _fXQuanEndGO) + 1.0) * _fXQuanEndGO;
			}
	}
	g_print("X: %f,%f,%f\n", _fXEnd, _fXEndGO, _fXQuanEndGO);
}

void _changYBegionOrEndGO(gdouble m, gboolean isL)
{
	gdouble quan = _getQuan(m);   //��ø��������Ȩֵ
	if (isL)
	{
		//���ֵ�Ǵ�������
			if (quan < _fYQuanEndGO)
			{
				_fYQuanBeginGO = _fYQuanEndGO / 10.0f;
			}
			else if (quan > _fYQuanEndGO)
			{
				_fYQuanBeginGO = quan;
				_fYQuanEndGO = _fYQuanBeginGO / 10.0f;
			}
			else
			{
				_fYQuanBeginGO = _fYQuanEndGO;
			}
				if (m <= _fYQuanBeginGO && m >= -_fYQuanBeginGO)
				{
					_fYBeginGO = -_fYQuanBeginGO;
				}
				else
				{
					_fYBeginGO = ((int)(m / _fYQuanBeginGO) - 1) * _fYQuanBeginGO;
				}
	}
	else
	{
		//���ֵ�Ǵ��ұ����
			if (quan < _fYQuanBeginGO)
			{
				_fYQuanEndGO = _fYQuanBeginGO / 10.0f;
			}
			else if (quan > _fYQuanBeginGO)
			{
				_fYQuanEndGO = quan;
				_fYQuanBeginGO = _fYQuanEndGO / 10.0f;
			}
			else
			{
				_fYQuanEndGO = _fYQuanBeginGO;
			}
				if (m <= _fYQuanEndGO && m >= _fYQuanBeginGO)
				{
					_fYEndGO = _fYQuanEndGO;
				}
				else
				{
					_fYEndGO = ((int)(m / _fYQuanEndGO) + 1.0) * _fYQuanEndGO;
				}
	}
	g_print("Y: %f,%f,%f\n", _fYEnd, _fYEndGO, _fYQuanEndGO);
}

gdouble RegulateY(gdouble dMin, gdouble dMax, gint iMaxAxisNum)
{
	if (iMaxAxisNum<1 || dMax<dMin)
		return 1;

	gdouble dDelta = dMax - dMin;
	if (dDelta<1.0) //Modify this by your requirement.
	{
		dMax += (1.0 - dDelta) / 2.0;
		dMin -= (1.0 - dDelta) / 2.0;
	}
	dDelta = dMax - dMin;

	gint iExp = (gint)(log(dDelta) / log(10.0)) - 2;
	gdouble dMultiplier = pow(10, iExp);
	const gdouble dSolutions[] = { 1, 2, 2.5, 5, 10, 20, 25, 50, 100, 200, 250, 500, 1000, 1500, 2000, 2500, 5000};
	gint i;
	for (i = 0; i<sizeof(dSolutions) / sizeof(gdouble); i++)
	{
		gdouble dMultiCal = dMultiplier * dSolutions[i];
		if (((gint)(dDelta / dMultiCal) + 1) <= iMaxAxisNum)
		{
			break;
		}
	}

	gdouble dInterval = dMultiplier * dSolutions[i];

	gdouble dStartPoint = ((gint)ceil(dMin / dInterval) - 1) * dInterval;
	gint iAxisIndex;
	for (iAxisIndex = 0; 1; iAxisIndex++)
	{
		//g_print("%f", dStartPoint + dInterval*iAxisIndex);
		if (dStartPoint + dInterval*iAxisIndex > dMax) 
		{
			top_y=dStartPoint + dInterval*iAxisIndex;
			break;
		}
		//g_print(" | ");
	}
	//g_print("\n");
	return dInterval;
}

gdouble RegulateX(gdouble dMin, gdouble dMax, gint iMaxAxisNum)
{
	if (iMaxAxisNum<1 || dMax<dMin)
		return 0;

	gdouble dDelta = dMax - dMin;
	if (dDelta<1.0) //Modify this by your requirement.
	{
		dMax += (1.0 - dDelta) / 2.0;
		dMin -= (1.0 - dDelta) / 2.0;
	}
	dDelta = dMax - dMin;

	gint iExp = (gint)(log(dDelta) / log(10.0)) - 2;
	gdouble dMultiplier = pow(10, iExp);
	const gdouble dSolutions[] = { 1, 2, 2.5, 5, 10, 20, 25, 50, 100, 200, 250, 500, 1000, 1500, 2000, 2500, 5000 };
	gint i;
	for (i = 0; i<sizeof(dSolutions) / sizeof(gdouble); i++)
	{
		gdouble dMultiCal = dMultiplier * dSolutions[i];
		if (((gint)(dDelta / dMultiCal) + 1) <= iMaxAxisNum)
		{
			break;
		}
	}

	gdouble dInterval = dMultiplier * dSolutions[i];

	gdouble dStartPoint = ((gint)ceil(dMin / dInterval) - 1) * dInterval;
	gint iAxisIndex;
	for (iAxisIndex = 0; 1; iAxisIndex++)
	{
		//g_print("%f", dStartPoint + dInterval*iAxisIndex);
		if (dStartPoint + dInterval*iAxisIndex > dMax)
		{
			top_x = dStartPoint + dInterval*iAxisIndex;
			break;
		}
		//g_print(" | ");
	}
	//g_print("\n");
	return dInterval;
}

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
	gint j = 0;
	gchar c[32];
	gdouble max_y = 0, max_x = 0, Interval_x = 0, Interval_y = 0, big_y_sp = 0, big_x_sp = 0, width = 0, height = 0;
	gdouble Blank = 25;
	for (j = 0; j<8; j++)
	{
		recv_temp[j] = 0;
	}

	width = gtk_widget_get_allocated_width(widget);
	height = gtk_widget_get_allocated_height(widget);

	cairo_set_source_surface(cr, surface, 0, 0);
	cairo_paint(cr);

	for (j = 0; j < data_num; j++)
	{
		if (max_x < datas[j][0])
		{
			max_x = datas[j][0];
		}
		if (max_y < datas[j][3])
		{
			max_y = datas[j][3];
		}
	}

	Interval_y = RegulateY(1, max_y, 10);
	Interval_x = RegulateX(-0.00001, max_x, 8);

	big_y_sp = (height - 2 * Blank) / (top_y / Interval_y);
	big_x_sp = (width - 2 * Blank) / (top_x / Interval_x);

	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_set_line_width(cr, 0.5);
	cairo_rectangle(cr, Blank, Blank, width - 2 * Blank, height - 2 * Blank);/* Draw outer border */

	for (i = height - Blank; i>Blank - 1; i = i - big_y_sp)/* Draw Y-axis */
	{
		cairo_move_to(cr, Blank - 6, i);
		cairo_line_to(cr, width - Blank, i);
		cairo_move_to(cr, Blank - 25, i);
		cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
		cairo_set_font_size(cr, 12.0);
		sprintf(c, "%.0lf", y);
		y = y + Interval_y;
		cairo_show_text(cr, c);
	}
	for (i = height - Blank; i>Blank; i = i - big_y_sp / 10)
	{
		cairo_move_to(cr, Blank - 3, i);
		cairo_line_to(cr, Blank, i);
	}
	for (i = Blank; i <= (width - Blank); i = i + big_x_sp)/* Draw X-axis */
	{
		cairo_move_to(cr, i, Blank);
		cairo_line_to(cr, i, height - Blank + 6);
		cairo_move_to(cr, i - 10, height - Blank + 16);
		cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
		cairo_set_font_size(cr, 12.0);
		sprintf(c, "%.2lf", x);
		x = x + Interval_x;
		cairo_show_text(cr, c);
	}
	for (i = Blank; i<(width - Blank); i = i + big_x_sp / 10)
	{
		cairo_move_to(cr, i, height - Blank);
		cairo_line_to(cr, i, height - Blank + 3);
	}
	cairo_stroke(cr);

	recv_temp[0] = Blank;//x
	recv_temp[3] = height - Blank;//y
	cairo_set_source_rgb(cr, 0, 1, 0);
	cairo_set_line_width(cr, 1.5);
	for (j = 0; j < data_num; j++)/* drawing lines */
	{
		cairo_move_to(cr, datas[j][0] / top_x * (width - 2 * Blank) + Blank, height - Blank - datas[j][3] / top_y * (height - 2 * Blank));
		cairo_line_to(cr, recv_temp[0], recv_temp[3]);
		recv_temp[0] = datas[j][0] / top_x * (width - 2 * Blank) + Blank;//x
		recv_temp[3] = height - Blank - datas[j][3] / top_y * (height - 2 * Blank);//y
	}
	cairo_stroke(cr);
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
#ifdef _LINUX_
	PangoLayout *layout;
#endif
	gint i;
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
#ifdef _LINUX_
		sprintf(c, "%d", y);
#endif
#ifndef _LINUX_
		sprintf(c, "%d", y);
#endif
		cairo_arc(cr, xc - 8, yc + 8, radius - 35, (36 * i - 90)  * (M_PI / 180.0), (36 * i - 90)  * (M_PI / 180.0));
		cairo_show_text(cr, c);
		cairo_stroke(cr);
	}

	cairo_set_font_size(cr, 15.0);
	cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_source_rgba(cr, 0, 0, 0, 1);
	cairo_move_to(cr, xc - 30, yc + 30);
	//const gchar* y="������(N)";
#ifdef _LINUX_
	layout = pango_cairo_create_layout(cr);
	pango_layout_set_text(layout, "������(N)", -1);
	pango_cairo_show_layout(cr, layout);
	g_object_unref(layout);
#endif
#ifndef _LINUX_
	cairo_show_text(cr, _("������(N)"));
#endif
	cairo_stroke(cr);

	cairo_set_source_rgb(cr, 1, 0, 0);
	cairo_set_line_width(cr, 6.0);
	//cairo_arc(cr, xc, yc, radius - 30, (179 + arc_i)* (M_PI / 180.0), (180 + arc_i) * (M_PI / 180.0));
	cairo_arc(cr, xc, yc, radius - 30, (360 * AD1 / 600 - 90)* (M_PI / 180.0), (360 * AD1 / 600 - 90) * (M_PI / 180.0));
	cairo_line_to(cr, xc, yc);
	cairo_stroke(cr);
	arc_i = arc_i + 10;
	if (arc_i == 360) arc_i = 0;

	cairo_set_line_width(cr, 5);
	cairo_set_source_rgb(cr, 0.7, 0.7, 0.7);
	cairo_rectangle(cr, 0, 0, width, height);
	cairo_stroke(cr);

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
#ifdef _LINUX_
	PangoLayout *layout;
#endif
	gdouble width, height;
	gchar c[8];
	width = gtk_widget_get_allocated_width(widget);
	height = gtk_widget_get_allocated_height(widget);
	cairo_set_line_width(cr, 5);
	cairo_set_source_rgb(cr, 0, 0, 0);	//black
	cairo_rectangle(cr, 0, 0, width / 4, height);//5, 12
	cairo_fill_preserve(cr);
	cairo_rectangle(cr, width / 4, 0, width / 4, height);//290,12
	cairo_fill_preserve(cr);
	cairo_rectangle(cr, width / 2, 0, width / 4, height);//575,12
	cairo_fill_preserve(cr);
	cairo_rectangle(cr, width / 4 * 3, 0, width / 4, height);
	cairo_fill_preserve(cr);
	cairo_stroke(cr);

	cairo_set_source_rgb(cr, 0.2, 1, 1);
	cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(cr, 50.0);
	cairo_move_to(cr, width / 4 - 170, height - 30);
#ifdef _LINUX_
	sprintf(c, "%.2f", AD1);
#endif
#ifndef _LINUX_
	sprintf(c, "%.2f", AD1);
#endif
	cairo_show_text(cr, c);
	cairo_move_to(cr, width / 4 * 2 - 170, height - 30);
#ifdef _LINUX_
	sprintf(c, "%.2f", P1);
#endif
#ifndef _LINUX_
	sprintf(c, "%.2f", P1);
#endif
	cairo_show_text(cr, c);
	cairo_move_to(cr, width / 4 * 3 - 150, height - 30);
	sprintf(c, "%.1lf", time_second);
	cairo_show_text(cr, c);
	cairo_move_to(cr, width - 170, height - 30);
#ifdef _LINUX_
	sprintf(c, "%.2f", P2);
#endif
#ifndef _LINUX_
	sprintf(c, "%.2f", P2);
#endif
	cairo_show_text(cr, c);
	cairo_stroke(cr);

	cairo_set_source_rgb(cr, 1, 1, 1);//white
	cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(cr, 20.0);
#ifdef _LINUX_
	layout = pango_cairo_create_layout(cr);
	pango_cairo_show_layout(cr, layout);
	cairo_move_to(cr, 0 + 5, height - 75);
	pango_layout_set_text(layout, "������(N)", -1);
	pango_cairo_show_layout(cr, layout);
	cairo_move_to(cr, width / 4 + 5, height - 75);
	pango_layout_set_text(layout, "����(mm)", -1);
	pango_cairo_show_layout(cr, layout);
	cairo_move_to(cr, width / 2 + 5, height - 75);
	pango_layout_set_text(layout, "ʱ��(s)", -1);
	pango_cairo_show_layout(cr, layout);
	cairo_move_to(cr, width / 4 * 3 + 5, height - 75);
	pango_layout_set_text(layout, "λ��(mm)", -1);
	pango_cairo_show_layout(cr, layout);
	g_object_unref(layout);
#endif
#ifndef _LINUX_
	cairo_move_to(cr, 0 + 5, height - 75);
	cairo_show_text(cr, _("������(N)"));
	cairo_move_to(cr, width / 4 + 5, height - 75);
	cairo_show_text(cr, _("����(mm)"));
	cairo_move_to(cr, width / 2 + 5, height - 75);
	cairo_show_text(cr, _("ʱ��(s)"));
	cairo_move_to(cr, width / 4 * 3 + 5, height - 75);
	cairo_show_text(cr, _("λ��(mm)"));
#endif

	cairo_stroke(cr);

	cairo_set_line_width(cr, 5);
	cairo_set_source_rgb(cr, 0.7, 0.7, 0.7);
	cairo_rectangle(cr, 0, 0, width / 4, height);//5, 12
	cairo_rectangle(cr, width / 4, 0, width / 4, height);//290,12
	cairo_rectangle(cr, width / 2, 0, width / 4, height);//575,12
	cairo_rectangle(cr, width / 4 * 3, 0, width / 4, height);
	cairo_stroke(cr);

	return FALSE;
}

gboolean time_handler3(GtkWidget *widget)
{
	gdouble width, height;
	width = gtk_widget_get_allocated_width(widget);
	height = gtk_widget_get_allocated_height(widget);
	if (surface3 == NULL) return FALSE;
	gtk_widget_queue_draw_area(widget, 0, 0, width, height);

	if (start == TRUE)
	{
		time_second = time_second + 0.1;
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
	GtkWidget *batch_label, *num_label, *time_label, *temp_label, *name_label, *shape_label, *outer_label, *thick_label, *span_label;
	GtkWidget *box;
	gint x, y, z;

	ip_menu_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_position(GTK_WINDOW(ip_menu_window), GTK_WIN_POS_CENTER);
	gtk_window_set_title(GTK_WINDOW(ip_menu_window), "Window For Setting");
	gtk_window_set_default_size(GTK_WINDOW(ip_menu_window), 250, 450);

	label1 = gtk_label_new("IP:");
	label2 = gtk_label_new("Port:");
	entries.IP = (GtkEntry *)gtk_entry_new();
	entries.Port = (GtkEntry *)gtk_entry_new();

	fixed = gtk_fixed_new();
	box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);

	entries.batch = (GtkEntry *)gtk_entry_new();
	batch_label = gtk_label_new(_("��������:"));
	entries.num = (GtkEntry *)gtk_entry_new();
	num_label = gtk_label_new(_("������:"));
	entries.time = (GtkEntry *)gtk_entry_new();
	time_label = gtk_label_new(_("ʵ������:"));
	entries.temp = (GtkEntry *)gtk_entry_new();
	temp_label = gtk_label_new(_("�¶�(��):"));
	entries.name = (GtkEntry *)gtk_entry_new();
	name_label = gtk_label_new(_("������:"));
	entries.combo = (GtkEntry *)gtk_combo_box_text_new_with_entry();
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(entries.combo), _("���"));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(entries.combo), _("Բ��"));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(entries.combo), _("�ܲ�"));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(entries.combo), _("����"));
	gtk_combo_box_set_active(GTK_COMBO_BOX(entries.combo), 0);
	shape_label = gtk_label_new(_("������״:"));
	entries.outer = (GtkEntry *)gtk_entry_new();
	outer_label = gtk_label_new(_("�������(mm):"));
	entries.thick = (GtkEntry *)gtk_entry_new();
	thick_label = gtk_label_new(_("�������(mm):"));
	entries.span = (GtkEntry *)gtk_entry_new();
	span_label = gtk_label_new(_("���(mm):"));

	close_button = gtk_button_new_with_label(_("ȷ��"));

	gtk_widget_set_size_request(close_button, 100, 20);

	x = 120;
	y = 0;
	z = 40;
	y = y + z;
	gtk_fixed_put(GTK_FIXED(fixed), label1, 20, y);
	gtk_fixed_put(GTK_FIXED(fixed), GTK_WIDGET(entries.IP), x, y);
	y = y + z;
	gtk_fixed_put(GTK_FIXED(fixed), label2, 20, y);
	gtk_fixed_put(GTK_FIXED(fixed), GTK_WIDGET(entries.Port), x, y);
	y = y + z;
	gtk_fixed_put(GTK_FIXED(fixed), batch_label, 20, y);
	gtk_fixed_put(GTK_FIXED(fixed), GTK_WIDGET(entries.batch), x, y);
	y = y + z;
	gtk_fixed_put(GTK_FIXED(fixed), num_label, 20, y);
	gtk_fixed_put(GTK_FIXED(fixed), GTK_WIDGET(entries.num), x, y);
	y = y + z;
	gtk_fixed_put(GTK_FIXED(fixed), time_label, 20, y);
	gtk_fixed_put(GTK_FIXED(fixed), GTK_WIDGET(entries.time), x, y);
	y = y + z;
	gtk_fixed_put(GTK_FIXED(fixed), temp_label, 20, y);
	gtk_fixed_put(GTK_FIXED(fixed), GTK_WIDGET(entries.temp), x, y);
	y = y + z;
	gtk_fixed_put(GTK_FIXED(fixed), name_label, 20, y);
	gtk_fixed_put(GTK_FIXED(fixed), GTK_WIDGET(entries.name), x, y);
	y = y + z;
	gtk_fixed_put(GTK_FIXED(fixed), shape_label, 20, y);
	gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(entries.combo), FALSE, FALSE, 1);
	gtk_fixed_put(GTK_FIXED(fixed), box, x, y);
	y = y + z;
	gtk_fixed_put(GTK_FIXED(fixed), outer_label, 20, y);
	gtk_fixed_put(GTK_FIXED(fixed), GTK_WIDGET(entries.outer), x, y);
	y = y + z;
	gtk_fixed_put(GTK_FIXED(fixed), thick_label, 20, y);
	gtk_fixed_put(GTK_FIXED(fixed), GTK_WIDGET(entries.thick), x, y);
	y = y + z;
	gtk_fixed_put(GTK_FIXED(fixed), span_label, 20, y);
	gtk_fixed_put(GTK_FIXED(fixed), GTK_WIDGET(entries.span), x, y);
	y = y + 60;
	gtk_fixed_put(GTK_FIXED(fixed), close_button, 20, y);

	gtk_container_add(GTK_CONTAINER(ip_menu_window), fixed);
	g_signal_connect(G_OBJECT(close_button), "clicked", G_CALLBACK(on_ip_button1_clicked), (gpointer)&entries);

	return ip_menu_window;
}

/* set ip address function */
void on_ip_menu_activate(GtkMenuItem* item, gpointer data)
{
	ip_menu_window = create_ip_menu_window();
	gtk_widget_show_all(ip_menu_window);
}

void socket_msg_handle(gint fd, socket_msg *msg, void *args)
{
	gdouble Ls = 0.0000;
	gdouble b = 0.0000;
	gdouble h = 0.0000;
	gdouble bufferIn[8];
	P1 = ((gdouble)((msg->data[1] & 0x7f) << 16 | msg->data[2] << 8 | msg->data[3]) / (gdouble)0x7fffff) * 600;
	//P1 = ((gdouble)(msg->data[0] << 24 | msg->data[1] << 16 | msg->data[2] << 8 | msg->data[3]) / (gdouble)0xffffffff) * 16777215 / 250;
	P2 = ((gdouble)(msg->data[4] << 24 | msg->data[5] << 16 | msg->data[6] << 8 | msg->data[7]) / (gdouble)0xffffffff) * 16777215 / 250;
	P3 = ((gdouble)(msg->data[8] << 24 | msg->data[9] << 16 | msg->data[10] << 8 | msg->data[11]) / (gdouble)0xffffffff) * 16777215 / 250;
	AD1 = ((gdouble)((msg->data[13] & 0x7f) << 16 | msg->data[14] << 8 | msg->data[15]) / (gdouble)0x7fffff) * 600;
	AD2 = ((gdouble)((msg->data[17] & 0x7f) << 16 | msg->data[18] << 8 | msg->data[19]) / (gdouble)0x7fffff) * 600;
	AD3 = ((gdouble)((msg->data[21] & 0x7f) << 16 | msg->data[22] << 8 | msg->data[23]) / (gdouble)0x7fffff) * 600;
	AD4 = ((gdouble)((msg->data[25] & 0x7f) << 16 | msg->data[26] << 8 | msg->data[27]) / (gdouble)0x7fffff) * 600;
	DI = msg->data[28];
	datas[data_num][0] = P1;
	datas[data_num][1] = P2;
	datas[data_num][2] = P3;
	datas[data_num][3] = AD1;
	datas[data_num][4] = AD2;
	datas[data_num][5] = AD3;
	datas[data_num][6] = AD4;
	datas[data_num][7] = DI;
	data_num++;
	bufferIn[0] = P1;
	bufferIn[1] = P2;
	bufferIn[2] = P3;
	bufferIn[3] = AD1;
	bufferIn[4] = AD2;
	bufferIn[5] = AD3;
	bufferIn[6] = AD4;
	bufferIn[7] = DI;
	//send_to_mysql(bufferIn); /* Record in the database */
#ifdef wei
	AD1 = ((gdouble)((msg->data[13] & 0x7f) << 16 | msg->data[14] << 8 | msg->data[15]) / (gdouble)0x7fffff) * 2.5;
	AD2 = ((gdouble)((msg->data[17] & 0x7f) << 16 | msg->data[18] << 8 | msg->data[19]) / (gdouble)0x7fffff) * 2.5;
	AD3 = ((gdouble)((msg->data[21] & 0x7f) << 16 | msg->data[22] << 8 | msg->data[23]) / (gdouble)0x7fffff) * 2.5;
	AD4 = ((gdouble)((msg->data[25] & 0x7f) << 16 | msg->data[26] << 8 | msg->data[27]) / (gdouble)0x7fffff) * 2.5;
	if ((fp = fopen(txt_name, "a")) == NULL)
	{
		printf("can not open file.!\n");
	}
	fprintf(fp, "%0.6lf,%0.6lf,%0.6lf,%0.6lf\r", AD1, AD2, AD3, AD4);
	fclose(fp);
#endif
	b = atof(gtk_entry_get_text(entries.outer));
	Ls = atof(gtk_entry_get_text(entries.span));
	h = atof(gtk_entry_get_text(entries.thick));
	if (Fbb_data<AD1)
	{
		Fbb_data = AD1;
		Rbb_data = 3 * Fbb_data*Ls / (2 * b*h*h);
	}
	if (((mE_k_data == 0) && AD1 / P1>mE_k_data))
	{
		mE_k_data = AD1 / P1;
	}
	else if ((mE_k_data != 0) && mE_k_data_ok == FALSE && (AD1 / P1)<(mE_k_data*0.95))
	{
		mE_k_data_ok = TRUE;
		mE_data = mE_k_data*Ls*Ls*Ls / (4 * b*h*h*h);
	}
}

/* A new thread,to receive message */
gpointer recv_func(gpointer arg)
{
	gint n = 0;

	gint len = 45;
	GError *error = NULL;
	socket_cache_init(cache, socket_msg_handle);
	while (1)
	{
		if (g_socket_receive(sock, (gchar *)bufferIn, 45, NULL, &error)<0)
		{
			perror("server recv error\n");
			break;
			//exit(1);
		}
		n = socket_msg_cpy_in(cache, bufferIn, len);
		//g_print("%x",bufferIn);
		if (n == 0) {
			return FALSE;//cache is full
		}
		//parse and handle socket message from cache
		socket_msg_parse(1, cache);

		if (n == len) {
			continue; //copy completed
		}
		//move the pointer
		bufferIn = bufferIn + n;
		len = len - n;
	}
}

/* Send function */
void send_func()//(const gchar *text)
{
	gchar a = 0x02;
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
	if (res == (gboolean)TRUE)
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
	gchar *text = NULL;
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
	start = TRUE;
}

/***************************************************************************************
*    Function:
*    Description:  create report
***************************************************************************************/

/* Report button function */
void on_report_button_clicked(GtkButton *button, gpointer user_data)
{
	cairo_surface_t *report_surface;
	cairo_t *cr;
	gdouble i = 0, x = 0, y = 0, Blank = 25, next = 25;
	gdouble big_sp, small_sp, width, height, tr_down, tr_right;
	gint j = 0, x_o;
	gchar c[8];
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

	recv[3] = datas[data_num - 1][3];
	//if (num >= 1) /* Calculate the maximum of current data */
	//{
	//	recv[0] = datas[num - 1][0];
	//	recv[1] = datas[num - 1][1];
	//	recv[2] = datas[num - 1][2];
	//	if (recv[0]>recv[1])
	//	{
	//		if (recv[2]>recv[0]) biggest = recv[2];
	//		else biggest = recv[0];
	//	}
	//	else
	//	{
	//		if (recv[2]>recv[1]) biggest = recv[2];
	//		else biggest = recv[1];
	//	}
	//}
	//else
	biggest = 50;
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
	if (data_num>700)
	{
		x = ((data_num - 700) / 100 + 1) * 100;
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

		if (data_num>700)/* X-axis starting value adjustment */
		{
			x_o = ((data_num - 700) / 100 + 1) * 100;
		}
		else x_o = 0;

		next = 48;
		for (j = x_o; j<data_num; j++)
		{
			cairo_set_source_rgb(cr, 0, 1, 0);/* Draw green line pulse1 */
			cairo_set_line_width(cr, 1.2);
			recv[3] = datas[j][3];
			cairo_move_to(cr, next / 2 + tr_right, tr_down + height - Blank - last_point[0] * small_sp);
			next++;
			cairo_line_to(cr, next / 2 + tr_right, tr_down + height - Blank - recv[3] * small_sp);
			last_point[0] = recv[3];
			cairo_stroke(cr);

			//cairo_set_source_rgb(cr, 0, 1, 0);/* Draw green line pulse1 */
			//cairo_set_line_width(cr, 1.2);
			//recv[0] = datas[j][0];
			//cairo_move_to(cr, next / 2 + tr_right, tr_down + height - Blank - last_point[0] * small_sp);
			//next++;
			//cairo_line_to(cr, next / 2 + tr_right, tr_down + height - Blank - recv[0] * small_sp);
			//last_point[0] = recv[0];
			//cairo_stroke(cr);

			//next--;
			//cairo_set_source_rgb(cr, 1, 0, 0);/* Draw red line pulse2 */
			//cairo_set_line_width(cr, 1.2);
			//recv[1] = datas[j][1];
			//cairo_move_to(cr, next / 2 + tr_right, tr_down + height - Blank - last_point[1] * small_sp);
			//next++;
			//cairo_line_to(cr, next / 2 + tr_right, tr_down + height - Blank - recv[1] * small_sp);
			//last_point[1] = recv[1];
			//cairo_stroke(cr);

			//next--;
			//cairo_set_source_rgb(cr, 0, 0, 1);/* Draw blue line pulse3 */
			//cairo_set_line_width(cr, 1.2);
			//recv[2] = datas[j][2];
			//cairo_move_to(cr, next / 2 + tr_right, tr_down + height - Blank - last_point[2] * small_sp);
			//next++;
			//cairo_line_to(cr, next / 2 + tr_right, tr_down + height - Blank - recv[2] * small_sp);
			//last_point[2] = recv[2];
			//cairo_stroke(cr);
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
	gchar c[16];
	gchar d[16];
	gchar e[16];
	GtkWidget *report_window;
	GtkWidget *fixed;
	GtkWidget *report_button;
	GtkWidget *batch1, *num1, *time1, *temp1, *name1, *shape1, *outer1, *thick1, *span1, *Fbb, *sigma, *Eb;
	GtkWidget *batch_label, *num_label, *time_label, *temp_label, *name_label, *shape_label, *outer_label, *thick_label, *span_label, *Fbb_label, *sigma_label, *Eb_label;
	gint x, y, z;

	report_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_position(GTK_WINDOW(report_window), GTK_WIN_POS_CENTER);
	fixed = gtk_fixed_new();
	report_button = gtk_button_new_with_label(_("��������"));

	batch1 = gtk_label_new(gtk_entry_get_text(entries.batch));
	batch_label = gtk_label_new(_("��������:"));
	num1 = gtk_label_new(gtk_entry_get_text(entries.num));
	num_label = gtk_label_new(_("������:"));
	time1 = gtk_label_new(gtk_entry_get_text(entries.time));
	time_label = gtk_label_new(_("ʵ������:"));
	temp1 = gtk_label_new(gtk_entry_get_text(entries.temp));
	temp_label = gtk_label_new(_("�¶�(��):"));
	name1 = gtk_label_new(gtk_entry_get_text(entries.name));
	name_label = gtk_label_new(_("������:"));
	shape1 = gtk_label_new(gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(entries.combo)));
	shape_label = gtk_label_new(_("������״:"));
	outer1 = gtk_label_new(gtk_entry_get_text(entries.outer));
	outer_label = gtk_label_new(_("�������(mm):"));
	thick1 = gtk_label_new(gtk_entry_get_text(entries.thick));
	thick_label = gtk_label_new(_("�������(mm):"));
	span1 = gtk_label_new(gtk_entry_get_text(entries.span));
	span_label = gtk_label_new(_("���(mm):"));
#ifdef _LINUX_
	sprintf(c, "%.4f", Fbb_data);
#endif
#ifndef _LINUX_
	sprintf(c, "%.4f", Fbb_data);
#endif
	Fbb = gtk_label_new((const gchar *)c);
	Fbb_label = gtk_label_new(_("Fbb(N):"));
#ifdef _LINUX_
	sprintf(d, "%.4f", Rbb_data);
#endif
#ifndef _LINUX_
	sprintf(d, "%.4f", Rbb_data);
#endif
	sigma = gtk_label_new((const gchar *)d);
	sigma_label = gtk_label_new(_("Rbb(MPa):"));
#ifdef _LINUX_
	sprintf(e, "%.4f", mE_data);
#endif
#ifndef _LINUX_
	sprintf(e, "%.4f", mE_data);
#endif
	Eb = gtk_label_new((const gchar *)e);
	Eb_label = gtk_label_new(_("mE(MPa):"));

	gtk_window_set_title(GTK_WINDOW(report_window), "Window For Report");
	gtk_window_set_default_size(GTK_WINDOW(report_window), 120, 500);

	gtk_widget_set_size_request(report_button, 100, 20);

	x = 150;
	y = 30;
	z = 40;
	gtk_fixed_put(GTK_FIXED(fixed), batch_label, 20, y);
	gtk_fixed_put(GTK_FIXED(fixed), batch1, x, y);
	y = y + z;
	gtk_fixed_put(GTK_FIXED(fixed), num_label, 20, y);
	gtk_fixed_put(GTK_FIXED(fixed), num1, x, y);
	y = y + z;
	gtk_fixed_put(GTK_FIXED(fixed), time_label, 20, y);
	gtk_fixed_put(GTK_FIXED(fixed), time1, x, y);
	y = y + z;
	gtk_fixed_put(GTK_FIXED(fixed), temp_label, 20, y);
	gtk_fixed_put(GTK_FIXED(fixed), temp1, x, y);
	y = y + z;
	gtk_fixed_put(GTK_FIXED(fixed), name_label, 20, y);
	gtk_fixed_put(GTK_FIXED(fixed), name1, x, y);
	y = y + z;
	gtk_fixed_put(GTK_FIXED(fixed), shape_label, 20, y);
	gtk_fixed_put(GTK_FIXED(fixed), shape1, x, y);
	y = y + z;
	gtk_fixed_put(GTK_FIXED(fixed), outer_label, 20, y);
	gtk_fixed_put(GTK_FIXED(fixed), outer1, x, y);
	y = y + z;
	gtk_fixed_put(GTK_FIXED(fixed), thick_label, 20, y);
	gtk_fixed_put(GTK_FIXED(fixed), thick1, x, y);
	y = y + z;
	gtk_fixed_put(GTK_FIXED(fixed), span_label, 20, y);
	gtk_fixed_put(GTK_FIXED(fixed), span1, x, y);
	y = y + z;
	gtk_fixed_put(GTK_FIXED(fixed), Fbb_label, 20, y);
	gtk_fixed_put(GTK_FIXED(fixed), Fbb, x, y);
	y = y + z;
	gtk_fixed_put(GTK_FIXED(fixed), sigma_label, 20, y);
	gtk_fixed_put(GTK_FIXED(fixed), sigma, x, y);
	y = y + z;
	gtk_fixed_put(GTK_FIXED(fixed), Eb_label, 20, y);
	gtk_fixed_put(GTK_FIXED(fixed), Eb, x, y);
	y = y + z;
	gtk_fixed_put(GTK_FIXED(fixed), report_button, 20, y);

	gtk_container_add(GTK_CONTAINER(report_window), fixed);
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
	g_free(bufferIn);
	g_free(cache);
	gtk_main_quit();
}

gint main(gint argc, char *argv[])
{
	gint i = 0;
	GtkWidget *window;
	GtkWidget *label9, *label10, *label11, *label12;
	GtkWidget *conn_button, *close_button, *send_button, *pre_report_button;
	GtkWidget *rece_view;
	GtkWidget *da;
	GtkWidget *sector;
	GtkWidget *menubar;
	GtkWidget *menu1, *menu2, *menu3, *menu4, *menu5, *menu6, *menu7;
	GtkWidget *setmenu, *adjustmenu, *toolmenu, *winmenu, *helpmenu, *ipmenu, *exitmenu;
	GtkWidget *s_force_sensor, *s_extensometer, *sys_para, *analy_para, *force_verfic, *extensometer_verfic, *dis_verific, *compress_db, *i_o_db, *lock, *Float, *auto_arrange, *array_win, *move_up_left, *about, *reg;
#ifdef _LINUX_
	GtkWidget *box;
#endif

	GtkWidget *grid;
	GtkWidget *scrolled1;

	GtkWidget *num;

	gtk_init(&argc, &argv);
	//struct EntryStruct entries;
	struct EntryStruct1 entries1;

	datas = (gdouble **)g_malloc(sizeof(gdouble *) * 8000 * 600);//8000*600s
	for (i = 0; i<(8000 * 600); i++)
	{
		datas[i] = (gdouble *)g_malloc(sizeof(gdouble) * 8);
	}
	bufferIn = (guchar *)g_malloc(sizeof(guchar) * 45000);
	cache = (socket_cache *)g_malloc(sizeof(socket_cache) + 45000 * sizeof(gchar));

	for (i = 0; i<8; i++)
	{
		datas[0][i] = 0;
	}

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
#ifdef _LINUX_
	gtk_window_set_title(GTK_WINDOW(window), "Window For Fatigue-Test (Linux)");
	gtk_widget_set_size_request(window, 1200, 600);
#endif
#ifndef _LINUX_
	gtk_window_set_title(GTK_WINDOW(window), "Window For Fatigue-Test (Win32)");
	gtk_widget_set_size_request(window, 1200, 650);
#endif
	gtk_container_set_border_width(GTK_CONTAINER(window), 0);

	grid = gtk_grid_new();
#ifdef _LINUX_
	box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
#endif

	label9 = gtk_label_new("Messages:");
	label10 = gtk_label_new(_("������-�Ӷ�����"));
	label11 = gtk_label_new(_("������(N)"));
	gtk_label_set_angle((GtkLabel *)label11, 90);
	label12 = gtk_label_new(_("�Ӷ�(mm)"));
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

#ifdef wei
	sprintf(txt_name, "recv_data.csv");
	if ((fp = fopen(txt_name, "w+")) == NULL)
	{
		g_print("can not open file.!\n");
	}
	fprintf(fp, "AD1,AD2,AD3,AD4\r");
	fclose(fp);
#endif
	/* Get the buffer of textbox */
	show_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(rece_view));
	/* Set textbox to diseditable */
	gtk_text_view_set_editable(GTK_TEXT_VIEW(rece_view), FALSE);
	/* Scroll window */
	scrolled1 = gtk_scrolled_window_new(NULL, NULL);
	/* Create a textbox */
	//gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrolled1), rece_view);
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
	conn_button = gtk_button_new_with_label(_("��ʼ"));
	//gtk_button_set_relief(GTK_BUTTON(conn_button), GTK_RELIEF_NONE);
	g_signal_connect(G_OBJECT(conn_button), "clicked", G_CALLBACK(on_button1_clicked), NULL);//(gpointer)&entries);

																							 /* Create a new button that has a mnemonic key of Alt+C. */
	close_button = gtk_button_new_with_mnemonic("Close");
	//gtk_button_set_relief(GTK_BUTTON(close_button), GTK_RELIEF_NONE);
	g_signal_connect_swapped(G_OBJECT(close_button), "clicked", G_CALLBACK(destroy), (gpointer)window);
	g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(destroy), NULL);

	/* Create a new button that prepare for the report */
	pre_report_button = gtk_button_new_with_label(_("ֹͣ"));
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

	setmenu = gtk_menu_item_new_with_label(_(" ���� "));
	adjustmenu = gtk_menu_item_new_with_label(_(" ���� "));
	toolmenu = gtk_menu_item_new_with_label(_(" ���� "));
	winmenu = gtk_menu_item_new_with_label(_(" ���� "));
	helpmenu = gtk_menu_item_new_with_label(_(" ���� "));
	ipmenu = gtk_menu_item_new_with_label(_(" IP "));
	exitmenu = gtk_menu_item_new_with_label(_(" �˳� "));

	s_force_sensor = gtk_menu_item_new_with_label(_(" ѡ���������� "));
	s_extensometer = gtk_menu_item_new_with_label(_(" ѡ������� "));
	sys_para = gtk_menu_item_new_with_label(_(" ϵͳ���� "));
	analy_para = gtk_menu_item_new_with_label(_(" �������� "));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu1), s_force_sensor);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu1), s_extensometer);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu1), sys_para);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu1), analy_para);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(setmenu), menu1);

	force_verfic = gtk_menu_item_new_with_label(_(" ���������춨 "));
	extensometer_verfic = gtk_menu_item_new_with_label(_(" ����Ƽ춨 "));
	dis_verific = gtk_menu_item_new_with_label(_(" λ�Ƽ춨 "));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu2), force_verfic);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu2), extensometer_verfic);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu2), dis_verific);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(adjustmenu), menu2);

	compress_db = gtk_menu_item_new_with_label(_(" ѹ�����ݿ� "));
	i_o_db = gtk_menu_item_new_with_label(_(" ���ݿ⵼�뵼�� "));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu3), compress_db);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu3), i_o_db);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(toolmenu), menu3);

	lock = gtk_menu_item_new_with_label(_(" ���� "));
	Float = gtk_menu_item_new_with_label(_(" ���� "));
	auto_arrange = gtk_menu_item_new_with_label(_(" �Զ����� "));
	array_win = gtk_menu_item_new_with_label(_(" �����Ӵ��� "));
	move_up_left = gtk_menu_item_new_with_label(_(" �ƶ�����Ļ���Ͻ� "));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu4), lock);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu4), Float);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu4), auto_arrange);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu4), array_win);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu4), move_up_left);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(winmenu), menu4);

	about = gtk_menu_item_new_with_label(_(" ���� "));
	reg = gtk_menu_item_new_with_label(_(" ע�� "));
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

#ifdef _LINUX_
	gtk_widget_set_size_request(menubar, 1270, 30);
	gtk_box_pack_start(GTK_BOX(box), menubar, FALSE, FALSE, 1);
	gtk_grid_attach(GTK_GRID(grid), box, 0, 0, 1200, 30);
#endif
#ifndef _LINUX_
	gtk_grid_attach(GTK_GRID(grid), menubar, 0, 0, 1200, 30);
#endif
	gtk_grid_attach(GTK_GRID(grid), num, 0, 30, 1200, 100);
	gtk_grid_attach(GTK_GRID(grid), label10, 400, 120, 50, 50);
	gtk_grid_attach(GTK_GRID(grid), label11, 0, 400, 50, 10);
	gtk_grid_attach(GTK_GRID(grid), da, 30, 160, 900 - 40, 450);
	gtk_grid_attach(GTK_GRID(grid), label12, 400, 650, 50, 50);
	gtk_grid_attach(GTK_GRID(grid), sector, 900, 200, 300, 300);
	gtk_grid_attach(GTK_GRID(grid), conn_button, 960, 540, 70, 70);
	gtk_grid_attach(GTK_GRID(grid), pre_report_button, 1070, 540, 70, 70);

	gtk_grid_set_row_spacing(GTK_GRID(grid), 1);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 1);
	gtk_container_add(GTK_CONTAINER(window), grid);

	gtk_widget_show_all(window);

	gtk_main();

	return 0;
}
