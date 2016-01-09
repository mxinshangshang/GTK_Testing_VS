#pragma once
#ifndef _SOCKET_MSG_H
#define _SOCKET_MSG_H
#ifdef _cplusplus
extern "C" {
#endif


	//=============================================================================
	//define macros
	//=============================================================================

	//=====================================
	//socket message 



#define SOCKET_MSG_HEAD				0x55555555		//head format of one message
#define SOCKET_MSG_END				0xaaaaaaaa		//end format
#define SOCKET_MSG_HEAD_SIZE		4				//size of message head
#define SOCKET_MSG_FIRST_SIZE		3
#define SOCKET_MSG_TYPE_SIZE		1
#define SOCKET_MSG_SECOND_SIZE		4
#define SOCKET_MSG_END_SIZE			4				//size of message end

#define SOCKET_MSG_CACHE_SIZE		(45000)			//client cache size
#define SOCKET_MSG_DATA_SIZE		(29)			//size of data buffer in one message
	//size of format of one message
#define SOCKET_MSG_FORMAT_SIZE \
(SOCKET_MSG_HEAD_SIZE+ \
 SOCKET_MSG_FIRST_SIZE+ \
SOCKET_MSG_TYPE_SIZE+ \
SOCKET_MSG_SECOND_SIZE+ \
 SOCKET_MSG_END_SIZE)
	//total size of one message
#define SOCKET_MSG_SIZE \
(SOCKET_MSG_DATA_SIZE+ \
 SOCKET_MSG_FORMAT_SIZE)


	//=============================================================================
	//structure
	//=============================================================================


	typedef enum {
		SEARCH_HEAD,
		SEARCH_FIRST,
		SEARCH_TYPE,
		//SEARCH_CS,
		SEARCH_SECOND,
		SEARCH_END,
		SEARCH_NONE
	}cache_strategy;


	typedef struct {
		guchar data[SOCKET_MSG_DATA_SIZE];			//data
		gint len;
		guchar type;
	}socket_msg;


	typedef void(*tp_socket_msg_handle)(gint fd, socket_msg *msg, void *args);

	typedef struct {
		guchar buf[SOCKET_MSG_CACHE_SIZE]; 		//buffer for storing data read from client
		gint front;
		gint rear;
		gint current;
		gint len;
		gint tag;										//mark that whether the cache is full,1-full,0-not full
		cache_strategy strategy;
		tp_socket_msg_handle handle;
		void* args;										//external 	parameter
		socket_msg recv_msg;
	}socket_cache;



	//=============================================================================
	//function
	//=============================================================================

	//initialize the socket_msg structure
	void socket_msg_init(socket_msg *msg);

	//initialize the socket_cache structure
	void socket_cache_init(socket_cache *cache, tp_socket_msg_handle handle);

	//copy buffer to cache from buffer
	gint socket_msg_cpy_in(socket_cache *cache, guchar *buf, gint len);

	//copy data to buffer from cache
	gint socket_msg_cpy_out(socket_cache *cache, guchar *buf, gint start_index, gint len);

	//parsed the packaged data, and invoke callback function
	void socket_msg_parse(gint fd, socket_cache *cache);

	//copy the unparsed data to cache, and parsed them
	gint socket_msg_pre_parse(
		gint fd,
		socket_cache *cache,
		guchar *buf,
		gint len,
		void *args);

	//before you send data,you should package them
	void socket_msg_package(socket_msg *msg, guchar type, guchar *buf, gint len);


#ifdef _cplusplus
}
#endif

#endif
