#include <stdio.h>
#include <string.h>
#include <time.h>
#ifdef DMS
#include <curl/curl.h>
#include <cjson/cJSON.h>
#endif
#include <string>
#include "YMESdk.h"
#include "md5.h"

#ifdef DMS

static void printJson(cJSON * root)    //以递归的方式打印json的最内层键值对
{
	for (int i = 0; i < cJSON_GetArraySize(root); i++) //遍历最外层json键值对
		{
			cJSON * item = cJSON_GetArrayItem(root, i);
			if (cJSON_Object == item->type)      //如果对应键的值仍为cJSON_Object就递归调用printJson
			    printJson(item);
			else                                //值不为json对象就直接打印出键和值
				{
					printf("%s->", item->string);
					printf("%s\n", cJSON_Print(item));
				}
		}
}

static size_t submit_order_reply(void *ptr, size_t size, size_t nmemb, void *userp)
{
	YMESdk_Client *c = (YMESdk_Client *)userp;
	char a[65536];
	memcpy(a, ptr, size*nmemb);
	a[size*nmemb] = 0;
	
	cJSON * root = cJSON_Parse(a);
	if (!root)
	{
		_Pr("查询失败，[%s]", ptr);
	}
	else
		printJson(root);
	
	cJSON *rsp_code = cJSON_GetObjectItem(root, "code");
	if (!rsp_code)
	{
		_Pr("查询失败，YmeSdk未预料到的错误");
	}
	int rc = atoi(rsp_code->valuestring);
	//_Pr("resp code = %s", rsp_code->valuestring);
	c->resp_code = rc;

	if (rc == 0)
	{
		cJSON *js_obj = cJSON_GetObjectItem(root, "sendid");
		if (js_obj)
			strcpy(c->sendid, js_obj->valuestring);
	}

	return size*nmemb;
}

static size_t query_token_cb(void *ptr, size_t size, size_t nmemb, void *userp)
{
	YMESdk_Client *c = (YMESdk_Client *)userp;
	char a[65536];
	memcpy(a, ptr, size*nmemb);
	a[size*nmemb] = 0;
	
	cJSON * root = cJSON_Parse(a);
	printJson(root);
	
	cJSON *rsp_code = cJSON_GetObjectItem(root, "code");
	if (!rsp_code)
	{
		_Pr("查询失败，YmeSdk未预料到的错误");
		return 0;
	}
	int rc = atoi(rsp_code->valuestring);
	//_Pr("resp code = %s", rsp_code->valuestring);
	c->resp_code = rc;

	if (rc == 0)
	{
		cJSON *js_obj = cJSON_GetObjectItem(root, "token");
		if (js_obj)
			strcpy(c->token, js_obj->valuestring);
		
		js_obj = cJSON_GetObjectItem(root, "tmsIp");
		if (js_obj)
		{
			std::string tmsIp(js_obj->valuestring);
			size_t pos = tmsIp.find(':', 0);
			std::string ip = tmsIp.substr(0, pos);
			std::string port = tmsIp.substr(pos + 1);
			c->tms_port = std::stoi(port);
			strncpy(c->tms_ip, ip.c_str(), sizeof c->tms_ip);
		}
	}

	return size*nmemb;
}

int YMESubmitOrder(YMESdk_Client *c)
{
	CURL *curl;
	CURLcode res;
	
	//获取时间戳
	char timestamp[32];
	//char md5_1[16] = { 0 };
	
	(timestamp, 0, sizeof timestamp);
	time_t now;
	now = time(NULL);
	strcpy(c->reqid, std::to_string((unsigned long)now).c_str());
	////获取时间戳
	
	//const char *postpattern = "param={\"header\":{\"sign\":\"4ce40718df039f1462ceebc3b534f7e9\",\"partyId\":10000000},\"body\":{\"userdataList\":[{\"userPackage\":\"10\",\"mobiles\":\"15711005175\",\"mobileip\":\"124.202.201.14\"}],\"size\":1,\"type\":\"0\",\"requestid\":\"20180125175639487yztest7028\"}}";
	std::string signpattern("body{\"userdataList\":[{");
	
	int userdataSize = 0;
	for (int i = 0; i < c->item_count; i++)
	{
		OrderItem& item = c->items[i];
		if (i != 0)
			signpattern += ",";
		signpattern += "\"userPackage\":\"" + std::to_string(item.userPackage) + "\",";
		signpattern += "\"mobiles\":\"" + std::string(item.mobile) + "\"";
		//signpattern += "\"mobileip\":\"" + std::string(c->mobileip) + "\"";
	}
	signpattern += "}],";
	signpattern += std::string("\"type\":\"0\",");
	signpattern += "\"requestid\":\"" + std::string(c->reqid) + "\"}";
	
	signpattern += std::string("keysdfjiejfiwhie112partyId10000000requestid") + c->reqid;

	//根据时间生成md5


	_Pr("%s", signpattern.c_str());
	std::string postform("param={\"header\":{");

	//计算md5
	unsigned char cpattern[512];
	memcpy(cpattern, signpattern.c_str(), signpattern.length());
	MD5 ctx;
	ctx.update(cpattern, signpattern.length());
	ctx.finalize();
	ctx.raw_digest(c->req_md5);

	char string_md5[32];
	for (int i = 0; i < 16; i++)  
	{  
		sprintf(&string_md5[i * 2], "%02x", c->req_md5[i]);  
	}
	//先注释掉sign
	postform += "\"sign\":\"";
	postform += std::string(string_md5);
	postform += "\",";

	postform += "\"partyId\":10000000},\"body\":{\"";
	postform += std::string("") + "userdataList\":[";

	for (int i = 0; i < c->item_count; i++)
	{
		OrderItem& item = c->items[i];
		postform += std::string("{\"userPackage\":\"");
		postform += std::to_string(item.userPackage) + "\",\"mobiles\":\"";
		postform += std::string("") + item.mobile + "\"}";
		if (i != c->item_count - 1)
		{
			postform += std::string(",");
		}
	}
	postform += std::string("],");
	postform += std::string("\"type\":\"0\",");
	postform += std::string("\"requestid\":\"") + c->reqid + "\"}}";
	
	_Pr("%s", postform.c_str());
	
	//exit(0);
	curl = curl_easy_init();
	if (curl) {
		//<==test //curl_easy_setopt(curl, CURLOPT_URL, "http://192.168.1.3:8080/directSubmit/submit");
		curl_easy_setopt(curl, CURLOPT_URL, "http://121.41.109.179:10002/directSubmit/submit");
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postform.c_str());
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, submit_order_reply);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, c);
		curl_easy_setopt(curl, CURLOPT_POST, 1);

		/* Perform the request, res will get the return code */
		res = curl_easy_perform(curl);
		/* Check for errors */
		if (res != CURLE_OK)
			fprintf(stderr,
				"curl_easy_perform() failed: %s\n",
				curl_easy_strerror(res));

		/* always cleanup */
		curl_easy_cleanup(curl);
	}
	return 0;
}

int YMEReqToken(YMESdk_Client *c)
{
	CURL *curl;
	CURLcode res;
	
	std::string keyform("sdfjiejfiwhie112");
	std::string reqtokform("param={\"userid\":");
	reqtokform += std::string(c->appid) + ",\"mobile\":\"" + std::string(c->mobile) + "\"";
	reqtokform += ",\"sendid\":\"" +  std::string(c->sendid) + "\"";
	
	std::string signpattern("");
	signpattern += std::string(c->appid) + std::string(c->mobile) + keyform;
	
	//计算md5
	unsigned char cpattern[512];
	memcpy(cpattern, signpattern.c_str(), signpattern.length());
	MD5 ctx;
	ctx.update(cpattern, signpattern.length());
	ctx.finalize();
	ctx.raw_digest(c->req_md5);

	char string_md5[32];
	for (int i = 0; i < 16; i++)  
	{  
		sprintf(&string_md5[i * 2], "%02x", c->req_md5[i]);  
	}
	
	reqtokform += std::string(",\"sign\":\"") + string_md5 + "\"";
	reqtokform += std::string("}");
	
	_Pr("%s", reqtokform.c_str());
	
	curl = curl_easy_init();
	if (curl) {
		curl_easy_setopt(curl, CURLOPT_URL, "http://121.41.109.179:10003/direct_activequery/querystatus");
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, reqtokform.c_str());
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, query_token_cb);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, c);
		curl_easy_setopt(curl, CURLOPT_POST, 1);

		/* Perform the request, res will get the return code */
		res = curl_easy_perform(curl);
		/* Check for errors */
		if (res != CURLE_OK)
			fprintf(stderr,
				"curl_easy_perform() failed: %s\n",
				curl_easy_strerror(res));

		/* always cleanup */
		curl_easy_cleanup(curl);
	}
	return 0;
}


#endif