#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>

#include <string>
#include <iostream>

#include "YMESdk.h"
#include "YMEUtils.h"


void test_func(void* a)
{
	_Pr("sdktest function callback,%p", a);
	return ;
}


int main(int argc, char *argv[])
{
	setbuf(stdout, nullptr);
	
	char* _tmsip;
	if (argc > 1)
	{
		_tmsip = argv[1];
	}
	else
	{
		_tmsip = "47.97.44.44";
	}
	_Pr("tmsip %s", _tmsip);
	
#ifdef DMS
	//创建DMS客户端
	YMESdk_Client client;
	memset(&client, 0, sizeof client);
	//创建订单里的项
	OrderItem i;
	memset(&i, 0, sizeof i);
	i.userPackage = 1000;
	strcpy(i.mobile, "18292820258");

	client.items = &i;
	client.item_count = 1;

	//提交订单
	strcpy(client.appid, "10000000");
	strcpy(client.mobile, "18292820258");
	//YMESubmitOrder(&client);
	
	//查询token
	//
	
	//userid":10000000,"mobile":"18292820258","sendid":"D152283190111765934"
	strcpy(client.mobile, "18292820258");
	strcpy(client.sendid, "A152324043461860830");
	strcpy(client.reqid, "333333331333");
	//YMEReqToken(&client);
	
#endif
	
	char auth[32] = "83cfed9778bf4a77";

	//<================= 1 YME Environment startup
	YMEInit();
	YMEStart();

	YMESet("appid", "10000000");
	YMESet("phone", "15711005176");
	YMESet("auth",  auth);
	//YMESet("Tmsip", "47.97.44.44");
	YMESet("Tmsip", _tmsip);
	YMESet("Tmsport", "10180");
	
	U64 cost;
	
	for (int j = 1; j < 20; j++)
	{
		YMERptTraffCost(&cost);
		printf("traffic cost is %llu\n", cost);
		sleep(20);
	}
	
	while (1)
		sleep(1);
	
	char *host =    "bbs.chinaunix.net";
	
	//char *host =    "bbs.csdn.net";
	/*char *request = "GET /thread-1507022-1-1.html HTTP/1.1\r\n\
	 *	char *request = "GET /thread-1507022-1-1.html HTTP/1.1\r\n\
	 **/
	
	char *request = "GET /topics/380243447 HTTP/1.1\r\n\
Host: bbs.chinaunix.net\r\n\
Connection: keep-alive\r\n\
Cache-Control: max-age=0\r\n\
User-Agent: Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/63.0.3239.132 Safari/537.36\r\n\
Upgrade-Insecure-Requests: 1\r\n\
Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8\r\n\
Accept-Encoding: gzip, deflate\r\n\
Accept-Language: zh-CN,zh;q=0.9\r\n\
\r\n";
	
	char* request_content = "GET /testDirectBackend/news/editNews?id=135&queryId=&queryTitle=&startTime=&endTime=&pageNo=1&pageFlag=1\r\n\
Host: 120.55.162.100\r\n\
Connection: keep-alive\r\n\
Cache-Control: max-age=0\r\n\
User-Agent: Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/63.0.3239.132 Safari/537.36\r\n\
Upgrade-Insecure-Requests: 1\r\n\
Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8\r\n\
Accept-Encoding: gzip, deflate\r\n\
Accept-Language: zh-CN,zh;q=0.9\r\n\
\r\n";
	char* request_catagory = "GET /testDirectBackend/news/information\r\n\
Host: 120.55.162.100\r\n\
Connection: keep-alive\r\n\
Cache-Control: max-age=0\r\n\
User-Agent: Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/63.0.3239.132 Safari/537.36\r\n\
Upgrade-Insecure-Requests: 1\r\n\
Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8\r\n\
Accept-Encoding: gzip, deflate\r\n\
Accept-Language: zh-CN,zh;q=0.9\r\n\
\r\n";
	char* request_img = "GET /testDirectBackend/upload/image/20180319/1521423030064049869.png\r\n\
Host: 120.55.162.100\r\n\
Connection: keep-alive\r\n\
Cache-Control: max-age=0\r\n\
User-Agent: Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/63.0.3239.132 Safari/537.36\r\n\
Upgrade-Insecure-Requests: 1\r\n\
Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8\r\n\
Accept-Encoding: gzip, deflate\r\n\
Accept-Language: zh-CN,zh;q=0.9\r\n\
\r\n";

	char* host1 =  "test.icloudata.net";
	
	long mobile, loop;

		mobile = 15711005176;
		loop = 100;

	
	struct timeval t1, t2, t3;
	
	gettimeofday(&t1, NULL);
	
	//<================   Request Page
	U64 aval_traffic = 0;
	unsigned char* reply = NULL;
	size_t rlen = 0;
	
	YME_HANDLE hh = 0;
	
	hh=YMEReqPage(
		inet_addr(YMEGet("Tmsip")),
		htons(atol(YMEGet("Tmsport"))),
		atoll(YMEGet("Appid")),
		atoll(YMEGet("Phone")),
		YMEGet("Auth"),
		host1,
		strlen(host1),
		80,
		(unsigned char*)request_img,
		strlen(request_img),
		&aval_traffic,
		test_func,
		(void*)test_func,
		&reply,
		&rlen);
	
	if (hh < 0)
	{
		_Pr("error");
	}
	
	auto& pc = PageCache::Instance();

	std::vector<newsItem> catagory;
	std::vector<contentItem> content;
	std::string srequest((char*)reply, rlen);
	if (rlen > 0)
	{
		int ret1 = PageCache::Instance().parse_test_page(
			srequest,
			&catagory,
			&content,
			ECT_Content);
	}

	
	while (1)
	{
		sleep(20);
		//YMEConnectable();
	}

/*
	//
	long LoginHandle = YMELoginTms(
		inet_addr("139.196.252.62"),
		//inet_addr("127.0.0.1"),
		htons(10180),
		10000000,
		mobile,
		auth,
		host1,
		strlen(host1),
		80,
		&aval_traffic);
	
	//<================= 2 Connect YMESdk to TMS
	U64 aval_traffic;
	char traffic_tmp[32] = { 0 };
	long LoginHandle = YMELoginTms(
		inet_addr("139.196.252.62"),
		//inet_addr("127.0.0.1"),
		htons(10180),
		10000000,
		mobile,
		auth,
		host1,
		strlen(host1),
		80,
		&aval_traffic);
	
	if (LoginHandle < 0)
	{
		//if LoginHandle == RT_UserNotExist --> UserNotExist
//	    RT_UserNotExist     = -8,
//		RT_IncorrectToken   = -7,
//		RT_NoBalance        = -6,
//		RT_RemoteFinish     = -5,
//		RT_Exception        = -4,
		YMEDetach();
		return -1;
	}
	
	//<==end 2
	gettimeofday(&t2, NULL);
	
	std::string abc,def,ghi;

	//<================= 3 Send and receive Http request via TMS
	int ret = YMESend(LoginHandle,
		(unsigned char*)request_content,
		strlen(request_content),
		&reply,
		&rlen);
		
	if (ret == RT_Exception || ret == RT_Down)
	{
		//request failed and connection state was down
		YMEShutDown(LoginHandle);
	}
	else if (ret == -2)
	{
		//handle reply first
		std::vector<std::tuple<int, std::string, std::string>> catagory;
		std::vector<std::tuple<int, std::string>> content;
		parse_test_page(std::string((char*)reply,0,rlen), catagory, content, 0);
		
		//disconnect
		YMEShutDown(LoginHandle);
	}
	else
	{
		std::vector<std::tuple<int, std::string, std::string>> catagory;
		std::vector<std::tuple<int, std::string>> content;
		parse_test_page(std::string((char*)reply,0,rlen), catagory, content, 0);
		//handle reply first
		//std::cout << reply << std::endl;
		//free reply buff
		YMEFreeBuff(LoginHandle);
	}
	//<==end 3
	
	gettimeofday(&t3, NULL);
	//YMEShutDown(LoginHandle);

	//YMEWait();
*/
	sleep(6);
	/*_P("%d\.%03d,%d\.%03d,%d\.%03d",
	t1.tv_sec,
	t1.tv_usec,
	t2.tv_sec,
	t2.tv_usec,
	t3.tv_sec,
	t3.tv_usec);*/
	
	//<================= 4 Detach then exit
	YMEDetach();
	return 0;
}