#include <arpa/inet.h>
#include "YMEUtils.h"
#include "YMEDataTypes.h"
#include "ConnectMgr.h"

/*
 * note :
 * 
 * packet parse functions return 1s on OK
 * 
 **/

typedef struct {
	/* Pointer to where we are currently reading from */
	const unsigned char *curr;
	/* Number of bytes remaining */
	int remaining;
} PACKET;

/*
 * Returns the number of bytes remaining to be read in the PACKET
 */
static inline int PACKET_remaining(const PACKET *pkt)
{
	return pkt->remaining;
}

/* Peek ahead at 1 byte from |pkt| and store the value in |*data| */
static inline int PACKET_peek_1(const PACKET *pkt,
	unsigned int *data)
{
	if (!PACKET_remaining(pkt))
		return 0;

	*data = *pkt->curr;

	return 1;
}

/* Internal unchecked shorthand; don't use outside this file. */
static inline void packet_forward(PACKET *pkt, size_t len)
{
	pkt->curr += len;
	pkt->remaining -= len;
}

/* Get 1 byte from |pkt| and store the value in |*data| */
static inline int PACKET_get_1(PACKET *pkt, unsigned int *data)
{
	if (!PACKET_peek_1(pkt, data))
		return 0;

	packet_forward(pkt, 1);

	return 1;
}
/*
 * Peek ahead at 2 bytes in network order from |pkt| and store the value in
 * |*data|
 */
static inline int PACKET_peek_net_2(const PACKET *pkt,
	unsigned int *data)
{
	if (PACKET_remaining(pkt) < 2)
		return 0;

	*data = ((unsigned int)(*pkt->curr)) << 8;
	*data |= *(pkt->curr + 1);

	return 1;
}
/* Equivalent of n2s */
/* Get 2 bytes in network order from |pkt| and store the value in |*data| */
static inline int PACKET_get_net_2(PACKET *pkt, unsigned int *data)
{
	if (!PACKET_peek_net_2(pkt, data))
		return 0;

	packet_forward(pkt, 2);

	return 1;
}
static inline int PACKET_skip_field_1(PACKET *pkt)
{
	unsigned int data;
	if (!PACKET_get_1(pkt, &data))
		return 0;

	packet_forward(pkt, data);
	return 1;
}
static inline int PACKET_skip_field_2(PACKET *pkt)
{
	unsigned int data;
	if (!PACKET_peek_net_2(pkt, &data))
		return 0;

	packet_forward(pkt, 2 + data);
	return 1;
}


/* Peek ahead at |len| bytes from |pkt| and copy them to |data| */
static inline int PACKET_peek_copy_bytes(const PACKET *pkt,
	unsigned char *data,
	size_t len)
{
	if (PACKET_remaining(pkt) < len)
		return 0;

	memcpy(data, pkt->curr, len);

	return 1;
}

/*
 * Read |len| bytes from |pkt| and copy them to |data|.
 * The caller is responsible for ensuring that |data| can hold |len| bytes.
 */
static inline int PACKET_copy_bytes(PACKET *pkt,
	unsigned char *data,
	size_t len)
{
	if (!PACKET_peek_copy_bytes(pkt, data, len))
		return 0;

	packet_forward(pkt, len);

	return 1;
}

static inline int tls_parse_extension_host(
	PACKET *pkt,
	unsigned char *host,
	size_t* hostlen)
{
	unsigned int type;
	unsigned int tlen;
	
	while (PACKET_remaining(pkt) > 0)
	{
		PACKET_get_net_2(pkt, &type);
		if (type == 0)
		{
			if (!PACKET_get_net_2(pkt, &tlen)) {
				_Pr("get server name len error");
			}
			if (!PACKET_copy_bytes(pkt, host, tlen)) {
				_Pr("copy server name error");
			}
			*hostlen = tlen;
			return 1;
		}
	}
	return 0;
}

int Connect::DetactTlsClientHello(char* start, size_t packet_len, char* host, size_t* hostlen)
{
#define YM_CLIENT_HELLO 1
	unsigned int isv2;
	unsigned int mt;
	unsigned int legacy_version, ciphersuites, session_id;
	PACKET local_packet;
	PACKET *pkt = &local_packet;
	
	if (packet_len < 0x40)
	{
		_Pr("DetactTlsClientHello: wait more stream data");
		return -1;
	}
	TlsHdr *thdr = (TlsHdr*) start;
#define TLS_TYPE_HANDSHAKE 0x16
	if (thdr->content_type != TLS_TYPE_HANDSHAKE)
	{
		_Pr("not tls handshake");
		return -1;
	}
	
	CliHelloHdr *hhdr = (CliHelloHdr*) start + sizeof(TlsHdr);
#define TLS_CLIENT_HELLO_TYPE 1
	if (hhdr->handshake_type == TLS_CLIENT_HELLO_TYPE)
	{
		_Pr("not hello request");
		return -1;
	}

	unsigned int remain = ntohs(thdr->length);
	unsigned int packet_remain = packet_len - sizeof(TlsHdr) - sizeof(CliHelloHdr);
	
	pkt->curr = (const unsigned char*)start + sizeof(TlsHdr) + sizeof(CliHelloHdr);
	pkt->remaining = remain > packet_remain ? packet_remain : remain;
	
	if (!PACKET_get_1(pkt, &mt)
	|| mt != YM_CLIENT_HELLO) {
		_Pr("not hello packet");
	}
	
	if (!PACKET_get_net_2(pkt, &legacy_version)) {
		_Pr("error");
	}
	
	/* Parse the message and load client random. */

		/* Regular ClientHello. */
		#define RANDOM_SIZE 0x20
		packet_forward(pkt, RANDOM_SIZE);
		if (!PACKET_skip_field_1(pkt) /*session id*/) {
			_Pr("SSL_R_LENGTH_MISMATCH session id");
			goto err;
		}

		if (!PACKET_skip_field_2(pkt)  /*cipher suits*/) {
			_Pr("SSL_R_LENGTH_MISMATCH cipher suits");
			goto err;
		}

		if (!PACKET_skip_field_1(pkt)/*compression*/) {
			_Pr("SSL_R_LENGTH_MISMATCH compression");
			goto err;
		}

		/* Could be empty. */
		if (PACKET_remaining(pkt) == 0) {
			_Pr("SSL_R_NO_EXTENSION");
		}
		else {
			int ret = tls_parse_extension_host(pkt, (unsigned char*)host, hostlen);
			if(ret == 1)
				return 0;
		}
err:
	return -1;
}



////
////   
////
#if 1
#include <tuple>
#include <iostream>

newsItem* PageCache::GetNewsItem(int newsId)
{
	newsItem& n = catagory_[newsId];
	if (n.newsId == newsId)
		return &n;
	else
		return nullptr;
}

std::vector<contentItem>* PageCache::GetContentItems(int newsId)
{
	if (cached_page_.size() > newsId
		&& cached_page_[newsId] == true)
	{
		std::vector<contentItem>& c = content_[newsId];
		return &c;
	}
	return nullptr;
}

int PageCache::ParsePage(std::string input, int type, int newsId)
{
	int  ret = 0;
	if (type == ECT_Catagory)
	{
		ret = parse_test_page(input,
			&catagory_,
			nullptr,
			ECT_Catagory);
		if (ret > 0) {
			content_.resize(ret);
			cached_page_.resize(ret);
		}
	}
	else if (type == ECT_Content)
	{
		int ps = cached_page_.size();
		if (newsId > ps)
			return -1;
		ret = parse_test_page(input,
			nullptr,
			&content_[newsId],
			ECT_Content);
		cached_page_[newsId] = true;
	}
	return ret;
}

int PageCache::parse_test_page(std::string& input,
	std::vector<newsItem> *catagory,
	std::vector<contentItem> *content,
	int type /* 0-catagory   1-content*/)
{
	std::cout << "-------------\n"
			  << input
			  << std::endl;
	static int serial = 0;
	if (type == 2) return 0;
	else if (type == 0)  //Ä¿Â¼
		{
			size_t end = 0;
			size_t pos = input.find("</thead>");
			pos = input.find("checkbox", pos);
			while (pos < input.length())
			{
				pos = input.find("</td>", pos);
				pos = input.find("</td>", pos + 1);
				pos = input.find("<td>", pos);
				pos += strlen("<td>");
				end = input.find("</td>", pos);
				std::string title(input, pos, end - pos);

				pos = input.find("href=\"", end);
				pos += strlen("href=\"");
				end = input.find("\">", pos);

				std::string url(input, pos, end - pos);

				newsItem c;
				c.newsId = serial++;
				c.title = title;
				c.url = url;
				catagory->push_back(c);

				pos = input.find("checkbox", pos);
			}
		}
	else if (type == 1)  //Ò³Ãæ
		{
			std::string title("type=\"text\" name=\"title\" value=\"");
			size_t pos = input.find(title);
			pos += title.length();
			size_t end = input.find("\" />", pos);
			std::string rt_title(input, pos, end - pos);

			/*0-title 1-text 2-resource*/

			contentItem c;
			c.contentType = ECT_Title;
			c.content = rt_title;

			content->push_back(c);

			std::string content_start("\" id=\"content\">");
			std::string newline("<p>");
			std::string newline_e("</p>");
			pos = input.find(content_start, end);

			if (pos > input.length())
				return -1;
			pos += content_start.length();
			size_t curr_pos = end;
			typedef std::tuple<size_t, contentItem> Unit;

			std::vector<Unit> img, text;

			do
			{
				pos = input.find("<img src=\"", end);
				if (pos > input.length())
					break;
				pos += strlen("<img src=\"");
				end = input.find("\"", pos);

				std::string url(input, pos, end - pos);

				Unit u;
				std::get<0>(u) = pos;
				std::get<1>(u).content = url;
				std::get<1>(u).contentType = ECT_Image;
				img.push_back(u);
			} while (1);

			end = curr_pos;
			do
			{
				pos = input.find(newline, end);
				if (pos > input.length())
					break;
				pos += newline.length();

				end = input.find(newline_e, pos);

				std::string text_str(input, pos, end - pos);

				Unit u;
				std::get<0>(u) = pos;
				std::get<1>(u).content = text_str;
				std::get<1>(u).contentType = ECT_Text;
				text.push_back(u);
			} while (1);

			auto it = text.begin();
			auto im = img.begin();
			while (it != text.end() && im != img.end())
			{
				if (std::get<0>(*it) < std::get<0>(*im))
				{
					content->push_back(std::get<1>(*it));
					it++;
				}
				else
				{
					content->push_back(std::get<1>(*im));
					im++;
				}
			}
			while (it != text.end())
			{
				content->push_back(std::get<1>(*it));
				it++;
			}
			while (im != img.end())
			{
				content->push_back(std::get<1>(*im));
				im++;
			}
		}

	if (type == 0)
		return catagory->size();
	else if (type == 1)
		return content->size();
    
    return -1;
}

#endif
