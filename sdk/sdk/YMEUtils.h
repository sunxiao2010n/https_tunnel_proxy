#pragma once
#ifndef __YMEUTILES__
#define __YMEUTILES__

#include <memory.h>

#pragma packet(1)
struct TlsHdr
{
	unsigned char content_type;
	unsigned short version;
	unsigned short length;
};
struct CliHelloHdr
{
	unsigned char handshake_type;
	unsigned short length;
	unsigned short version;
};
#pragma packet()

const char* YMEReturnCode(long return_code);
int DetactTlsClientHello(char* start, size_t packet_len, char* host, size_t* hostlen);

#if 1
//	尸体，仅供观赏
#include <string>
#include <tuple>
#include <vector>

enum _EContentType
{
	ECT_Catagory = 0,
	ECT_Content = 1,
	ECT_Title = 2,
	ECT_Text  = 3,
	ECT_Image = 4,
};

struct newsItem {
	int newsId;
	std::string title;
	std::string	url; /* "/article/article-1.html" */
};

struct contentItem {
	int contentType;  /*0-title 1-text 2-resource*/
	std::string content;
};

class PageCache
{
public:
	std::vector<bool> cached_page_;
	std::vector<newsItem> catagory_;
	std::vector<std::vector<contentItem>> content_;
	
	int parse_test_page(std::string& input,
		std::vector<newsItem> *catagory,
		std::vector<contentItem> *content,
		int type /* 0-catagory   1-content*/);
	
	int ParsePage(std::string input, int type, int newsId);
	newsItem* GetNewsItem(int newsId);
	std::vector<contentItem>* GetContentItems(int newsId);
	
	static PageCache& Instance()
	{
		static PageCache t;
		return t;
	}
};
#endif

#endif