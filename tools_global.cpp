#include <netdb.h>
#include <json.h>
#include <sstream>
#include <syslog.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "tools_global.h"

#ifndef CLOUD_ROUTER_SERVER
#include "tools.h"
#include "common.h"
cThreadMonitor threadMonitor;
#endif


struct vm_pthread_struct {
	void *(*start_routine)(void *arg);
	void *arg;
	string description;
};
void *vm_pthread_create_start_routine(void *arg) {
	vm_pthread_struct thread_data = *(vm_pthread_struct*)arg;
	delete (vm_pthread_struct*)arg;
	#ifndef CLOUD_ROUTER_SERVER
	threadMonitor.registerThread(thread_data.description.c_str());
	#endif
	return(thread_data.start_routine(thread_data.arg));
}
int vm_pthread_create(const char *thread_description,
		      pthread_t *thread, pthread_attr_t *attr,
		      void *(*start_routine) (void *), void *arg,
		      const char *src_file, int src_file_line, bool autodestroy) {
	#ifndef CLOUD_ROUTER_SERVER
	if(sverb.thread_create && src_file && src_file_line) {
		syslog(LOG_NOTICE, "create thread %sfrom %s : %i", 
		       autodestroy ? "(autodestroy) " : "", src_file, src_file_line);
	}
	#endif
	bool create_attr = false;
	pthread_attr_t _attr;
	if(!attr && autodestroy) {
		pthread_attr_init(&_attr);
		pthread_attr_setdetachstate(&_attr, PTHREAD_CREATE_DETACHED);
		create_attr = true;
		attr = &_attr;
	}
	vm_pthread_struct *thread_data = new FILE_LINE(0) vm_pthread_struct;
	thread_data->start_routine = start_routine;
	thread_data->arg = arg;
	thread_data->description = thread_description;
	int rslt = pthread_create(thread, attr, vm_pthread_create_start_routine, thread_data);
	if(create_attr) {
		pthread_attr_destroy(&_attr);
	}
	return(rslt);
}


JsonItem::JsonItem(string name, string value, bool null) {
	this->name = name;
	this->value = value;
	this->null = null;
	this->parse(value);
}

void JsonItem::parse(string valStr) {
	////cerr << "valStr: " << valStr << endl;
	if(!((valStr[0] == '{' && valStr[valStr.length() - 1] == '}') ||
	     (valStr[0] == '[' && valStr[valStr.length() - 1] == ']'))) {
		return;
	}
	json_object * object = json_tokener_parse(valStr.c_str());
	if(!object) {
		return;
	}
	json_type objectType = json_object_get_type(object);
	////cerr << "type: " << objectType << endl;
	if(objectType == json_type_object) {
		lh_table *objectItems = json_object_get_object(object);
		struct lh_entry *objectItem = objectItems->head;
		while(objectItem) {
			string fieldName = (char*)objectItem->k;
			string value;
			bool null = false;
			if(objectItem->v) {
				if(json_object_get_type((json_object*)objectItem->v) == json_type_null) {
					null = true;
				} else {
					value = json_object_get_string((json_object*)objectItem->v);
				}
			} else {
				null = true;
			}
			////cerr << "objectItem: " << fieldName << " - " << (null ? "NULL" : value) << endl;
			JsonItem newItem(fieldName, value, null);
			this->items.push_back(newItem);
			objectItem = objectItem->next;
		}
	} else if(objectType == json_type_array) {
		int length = json_object_array_length(object);
		for(int i = 0; i < length; i++) {
			json_object *obj = json_object_array_get_idx(object, i);
			string value;
			bool null = false;
			if(obj) {
				if(json_object_get_type(obj) == json_type_null) {
					null = true;
				} else {
					value = json_object_get_string(obj);
				}
				////cerr << "arrayItem: " << i << " - " << (null ? "NULL" : value) << endl;
			} else {
				null = true;
			}
			stringstream streamIndexName;
			streamIndexName << i;
			JsonItem newItem(streamIndexName.str(), value, null);
			this->items.push_back(newItem);
		}
	}
	json_object_put(object);
}

JsonItem *JsonItem::getItem(string path, int index) {
	if(index >= 0) {
		stringstream streamIndexName;
		streamIndexName << index;
		path += '/' + streamIndexName.str();
	}
	JsonItem *item = this->getPathItem(path);
	if(item) {
		string pathItemName = this->getPathItemName(path);
		if(path.length()>pathItemName.length()) {
			return(item->getItem(path.substr(pathItemName.length()+1)));
		} else {
			return(item);
		}
	}
	return(NULL);
}

string JsonItem::getValue(string path, int index) {
	JsonItem *item = this->getItem(path, index);
	return(item ? item->value : "");
}

int JsonItem::getCount(string path) {
	JsonItem *item = this->getItem(path);
	return(item ? item->items.size() : 0);
}

JsonItem *JsonItem::getPathItem(string path) {
	string pathItemName = this->getPathItemName(path);
	for(int i = 0; i < (int)this->items.size(); i++) {
		if(this->items[i].name == pathItemName) {
			return(&this->items[i]);
		}
	}
	return(NULL);
}

string JsonItem::getPathItemName(string path) {
	string pathItemName = path;
	int sepPos = pathItemName.find('/');
	if(sepPos > 0) {
		pathItemName.resize(sepPos);
	}
	return(pathItemName);
}


JsonExport::JsonExport() {
	typeItem = _object;
}

JsonExport::~JsonExport() {
	while(items.size()) {
		delete (*items.begin());
		items.erase(items.begin());
	}
}

string JsonExport::getJson(JsonExport */*parent*/) {
	ostringstream outStr;
	if(!name.empty()) {
		outStr << '\"' << name << "\":";
	}
	if(typeItem == _object) {
		outStr << '{';
	} else if(typeItem == _array) {
		outStr << '[';
	}
	vector<JsonExport*>::iterator iter;
	for(iter = items.begin(); iter != items.end(); iter++) {
		if(iter != items.begin()) {
			outStr << ',';
		}
		outStr << (*iter)->getJson(this);
	}
	if(typeItem == _object) {
		outStr << '}';
	} else if(typeItem == _array) {
		outStr << ']';
	}
	return(outStr.str());
}

void JsonExport::add(const char *name, string content) {
	this->add(name, content.c_str());
}

void JsonExport::add(const char *name, const char *content) {
	JsonExport_template<string> *item = new FILE_LINE(38010) JsonExport_template<string>;
	item->setTypeItem(_string);
	item->setName(name);
	string content_esc;
	const char *ptr = content;
	while(*ptr) {
		switch (*ptr) {
		case '\\':	content_esc += "\\\\"; break;
		case '"':	content_esc += "\\\""; break;
		case '/':	content_esc += "\\/"; break;
		case '\b':	content_esc += "\\b"; break;
		case '\f':	content_esc += "\\f"; break;
		case '\n':	content_esc += "\\n"; break;
		case '\r':	content_esc += "\\r"; break;
		case '\t':	content_esc += "\\t"; break;
		default:	content_esc += *ptr; break;
		}
		++ptr;
	}
	item->setContent(content_esc);
	items.push_back(item);
}

void JsonExport::add(const char *name, int64_t content) {
	JsonExport_template<int64_t> *item = new FILE_LINE(0) JsonExport_template<int64_t>;
	item->setTypeItem(_number);
	item->setName(name);
	item->setContent(content);
	items.push_back(item);
}

void JsonExport::add(const char *name) {
	JsonExport_template<string> *item = new FILE_LINE(38011) JsonExport_template<string>;
	item->setTypeItem(_null);
	item->setName(name);
	item->setContent("null");
	items.push_back(item);
}

JsonExport *JsonExport::addArray(const char *name) {
	JsonExport *item = new FILE_LINE(38012) JsonExport;
	item->setTypeItem(_array);
	item->setName(name);
	items.push_back(item);
	return(item);
}

JsonExport *JsonExport::addObject(const char *name) {
	JsonExport *item = new FILE_LINE(38013) JsonExport;
	item->setTypeItem(_object);
	item->setName(name);
	items.push_back(item);
	return(item);
}

void JsonExport::addJson(const char *name, const string &content) {
	this->addJson(name, content.c_str());
}

void JsonExport::addJson(const char *name, const char *content) {
	JsonExport_template<string> *item = new FILE_LINE(38014) JsonExport_template<string>;
	item->setTypeItem(_json);
	item->setName(name);
	item->setContent(string(content));
	items.push_back(item);
}

template <class type_item>
string JsonExport_template<type_item>::getJson(JsonExport *parent) {
	ostringstream outStr;
	if(parent->getTypeItem() != _array || !name.empty()) {
		outStr << '\"' << name << "\":";
	}
	if(typeItem == _null) {
		outStr << "null";
	} else {
		if(typeItem == _string) {
			outStr << '\"';
		}
		outStr << content;
		if(typeItem == _string) {
			outStr << '\"';
		}
	}
	return(outStr.str());
}


string intToString(int i) {
	ostringstream outStr;
	outStr << i;
	return(outStr.str());
}

string intToString(long long i) {
	ostringstream outStr;
	outStr << i;
	return(outStr.str());
}

string intToString(u_int16_t i) {
	ostringstream outStr;
	outStr << i;
	return(outStr.str());
}

string intToString(u_int32_t i) {
	ostringstream outStr;
	outStr << i;
	return(outStr.str());
}

string intToString(u_int64_t i) {
	ostringstream outStr;
	outStr << i;
	return(outStr.str());
}

string floatToString(double d) {
	ostringstream outStr;
	outStr << d;
	return(outStr.str());
}

string pointerToString(void *p) {
	char buff[100];
	snprintf(buff, sizeof(buff), "%p", p);
	buff[sizeof(buff) - 1] = 0;
	return(buff);
}

string boolToString(bool b) {
	if (b) {
		return("true");
	} else  {
		return("false");
	}
}


string inet_ntostring(u_int32_t ip) {
	struct in_addr in;
	in.s_addr = htonl(ip);
	return(inet_ntoa(in));
}

u_int32_t inet_strington(const char *ip) {
	in_addr ips;
	inet_aton(ip, &ips);
	return(htonl(ips.s_addr));
}

void xorData(u_char *data, size_t dataLen, const char *key, size_t keyLength, size_t initPos) {
	for(size_t i = 0; i < dataLen; i++) {
		data[i] = data[i] ^ key[(initPos + i) % keyLength];
	}
}


string &find_and_replace(string &source, const string find, string replace, unsigned *counter_replace) {
	if(counter_replace) {
		*counter_replace = 0;
	}
 	size_t j = 0;
	for ( ; (j = source.find( find, j )) != string::npos ; ) {
		source.replace( j, find.length(), replace );
		j += replace.length();
		if(counter_replace) {
			++*counter_replace;
		}
	}
	return(source);
}

string find_and_replace(const char *source, const char *find, const char *replace, unsigned *counter_replace) {
	string s_source = source;
	find_and_replace(s_source, find, replace, counter_replace);
	return(s_source);
}


std::string &trim(std::string &s, const char *trimChars) {
	if(!s.length()) {
		 return(s);
	}
	if(!trimChars) {
		trimChars = "\r\n\t ";
	}
	size_t length = s.length();
	size_t trimCharsLeft = 0;
	while(trimCharsLeft < length && strchr(trimChars, s[trimCharsLeft])) {
		++trimCharsLeft;
	}
	if(trimCharsLeft) {
		s = s.substr(trimCharsLeft);
		length = s.length();
	}
	size_t trimCharsRight = 0;
	while(trimCharsRight < length && strchr(trimChars, s[length - trimCharsRight - 1])) {
		++trimCharsRight;
	}
	if(trimCharsRight) {
		s = s.substr(0, length - trimCharsRight);
	}
	return(s);
}

std::string trim_str(std::string s, const char *trimChars) {
	return(trim(s, trimChars));
}

std::vector<std::string> &split(const std::string &s, char delim, std::vector<std::string> &elems) {
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        elems.push_back(item);
    }
    return elems;
}

std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> elems;
    split(s, delim, elems);
    return elems;
}

std::vector<std::string> &split(const char *s, const char *delim, std::vector<std::string> &elems, bool enableTrim, bool useEmptyItems) {
	char *p = (char*)s;
	int delim_length = strlen(delim);
	while(p) {
		char *next_delim = strstr(p, delim);
		string elem = next_delim ?
			       std::string(p).substr(0, next_delim - p) :
			       std::string(p);
		if(enableTrim) {
			trim(elem);
		}
		if(useEmptyItems || elem.length()) {
			elems.push_back(elem);
		}
		p = next_delim ? next_delim + delim_length : NULL;
	}
	return elems;
}

std::vector<std::string> split(const char *s, const char *delim, bool enableTrim, bool useEmptyItems) {
	std::vector<std::string> elems;
	split(s, delim, elems, enableTrim, useEmptyItems);
	return elems;
}

std::vector<std::string> split(const char *s, std::vector<std::string> delim, bool enableTrim, bool useEmptyItems, bool enableTrimString) {
	vector<std::string> elems;
	string elem = s;
	if(enableTrimString) {
		trim(elem);
	}
	elems.push_back(elem);
	for(size_t i = 0; i < delim.size(); i++) {
		vector<std::string> _elems;
		for(size_t j = 0; j < elems.size(); j++) {
			vector<std::string> __elems = split(elems[j].c_str(), delim[i].c_str(), enableTrim, useEmptyItems);
			for(size_t k = 0; k < __elems.size(); k++) {
				_elems.push_back(__elems[k]);
			}
		}
		elems = _elems;
	}
	return(elems);
}

std::vector<int> split2int(const std::string &s, std::vector<std::string> delim, bool enableTrim) {
    std::vector<std::string> tmpelems = split(s.c_str(), delim, enableTrim);
    std::vector<int> elems;
    for (uint i = 0; i < tmpelems.size(); i++) {
	elems.push_back(atoi(tmpelems.at(i).c_str()));
    }
    return elems;
}

std::vector<int> split2int(const std::string &s, char delim) {
    std::vector<std::string> tmpelems;
    split(s, delim, tmpelems);
    std::vector<int> elems;
    for (uint i = 0; i < tmpelems.size(); i++) {
	elems.push_back(atoi(tmpelems.at(i).c_str()));
    }
    return elems;
}


bool check_regexp(const char *pattern) {
	regex_t re;
	if(regcomp(&re, pattern, REG_EXTENDED | REG_ICASE) != 0) {
		return(false);
	}
	regfree(&re);
	return(true);
}

int reg_match(const char *string, const char *pattern, const char *file, int line) {
	int status;
	regex_t re;
	if(regcomp(&re, pattern, REG_EXTENDED | REG_NOSUB | REG_ICASE) != 0) {
		static u_long lastTimeSyslog = 0;
		u_long actTime = getTimeMS();
		if(actTime - 1000 > lastTimeSyslog) {
			if(file) {
				syslog(LOG_ERR, "regcomp %s error in reg_match - call from %s : %i", pattern, file, line);
			} else {
				syslog(LOG_ERR, "regcomp %s error in reg_match", pattern);
			}
			lastTimeSyslog = actTime;
		}
		return(0);
	}
	status = regexec(&re, string, (size_t)0, NULL, 0);
	regfree(&re);
	return(status == 0);
}

int reg_match(const char *str, const char *pattern, vector<string> *matches, bool ignoreCase, const char *file, int line) {
	matches->clear();
	int status;
	regex_t re;
	if(regcomp(&re, pattern, REG_EXTENDED | (ignoreCase ? REG_ICASE: 0)) != 0) {
		static u_long lastTimeSyslog = 0;
		u_long actTime = getTimeMS();
		if(actTime - 1000 > lastTimeSyslog) {
			if(file) {
				syslog(LOG_ERR, "regcomp %s error in reg_replace - call from %s : %i", pattern, file, line);
			} else {
				syslog(LOG_ERR, "regcomp %s error in reg_match", pattern);
			}
			lastTimeSyslog = actTime;
		}
		return(-1);
	}
	int match_max = 20;
	regmatch_t match[match_max];
	memset(match, 0, sizeof(match));
	status = regexec(&re, str, match_max, match, 0);
	regfree(&re);
	if(status == 0) {
		int match_count = 0;
		for(int i = 0; i < match_max; i ++) {
			if(match[i].rm_so == -1 && match[i].rm_eo == -1) {
				break;
			}
			if(match[i].rm_eo > match[i].rm_so) {
				matches->push_back(string(str).substr(match[i].rm_so, match[i].rm_eo - match[i].rm_so));
				++match_count;
			}
		}
		return(match_count);
	}
	return(0);
}

string reg_replace(const char *str, const char *pattern, const char *replace, const char *file, int line) {
	int status;
	regex_t re;
	if(regcomp(&re, pattern, REG_EXTENDED | REG_ICASE) != 0) {
		static u_long lastTimeSyslog = 0;
		u_long actTime = getTimeMS();
		if(actTime - 1000 > lastTimeSyslog) {
			if(file) {
				syslog(LOG_ERR, "regcomp %s error in reg_replace - call from %s : %i", pattern, file, line);
			} else {
				syslog(LOG_ERR, "regcomp %s error in reg_replace", pattern);
			}
			lastTimeSyslog = actTime;
		}
		return("");
	}
	int match_max = 20;
	regmatch_t match[match_max];
	memset(match, 0, sizeof(match));
	status = regexec(&re, str, match_max, match, 0);
	regfree(&re);
	if(status == 0) {
		string rslt = replace;
		int match_count = 0;
		for(int i = 0; i < match_max; i ++) {
			if(match[i].rm_so == -1 && match[i].rm_eo == -1) {
				break;
			}
			++match_count;
		}
		for(int i = match_count - 1; i > 0; i--) {
			for(int j = 0; j < 2; j++) {
				char findStr[10];
				snprintf(findStr, sizeof(findStr), j ? "{$%i}" : "$%i", i);
				size_t findPos;
				while((findPos = rslt.find(findStr)) != string::npos) {
					rslt.replace(findPos, strlen(findStr), string(str).substr(match[i].rm_so, match[i].rm_eo - match[i].rm_so));
				}
			}
		}
		return(rslt);
	}
	return("");
}


cRegExp::cRegExp(const char *pattern, eFlags flags,
		 const char *file, int line) {
	this->pattern = pattern ? pattern : "";
	this->flags = flags;
	regex_create();
	if(regex_error) {
		static u_long lastTimeSyslog = 0;
		u_long actTime = getTimeMS();
		if(actTime - 1000 > lastTimeSyslog) {
			if(file) {
				syslog(LOG_ERR, "regcomp %s error in cRegExp - call from %s : %i", pattern, file, line);
			} else {
				syslog(LOG_ERR, "regcomp %s error in cRegExp", pattern);
			}
			lastTimeSyslog = actTime;
		}
	}
}

cRegExp::~cRegExp() {
	regex_delete();
}

bool cRegExp::regex_create() {
	if(regcomp(&regex, pattern.c_str(), REG_EXTENDED | ((flags & _regexp_icase) ? REG_ICASE : 0) | ((flags & _regexp_sub) ? 0 : REG_NOSUB)) == 0) {
		regex_init = true;
		regex_error = false;
	} else {
		regex_error = true;
		regex_init = false;
	}
	return(regex_init);
}

void cRegExp::regex_delete() {
	if(regex_init) {
		regfree(&regex);
		regex_init = false;
	}
	regex_error = false;
}

int cRegExp::match(const char *subject, vector<string> *matches) {
	if(matches) {
		matches->clear();
	}
	if(regex_init) {
		int match_max = 20;
		regmatch_t match[match_max];
		memset(match, 0, sizeof(match));
		if(regexec(&regex, subject, match_max, match, 0) == 0) {
			if(flags & _regexp_matches) {
				int match_count = 0;
				for(int i = 0; i < match_max; i ++) {
					if(match[i].rm_so == -1 && match[i].rm_eo == -1) {
						break;
					}
					if(match[i].rm_eo > match[i].rm_so) {
						if(matches) {
							matches->push_back(string(subject).substr(match[i].rm_so, match[i].rm_eo - match[i].rm_so));
						}
						++match_count;
					}
				}
				return(match_count);
			} else  {
				return(1);
			}
		} else {
			return(0);
		}
	}
	return(-1);
}


cGzip::cGzip() {
	operation = _na;
	zipStream = NULL;
	destBuffer = NULL;
}

cGzip::~cGzip() {
	term();
}

bool cGzip::compress(u_char *buffer, size_t bufferLength, u_char **cbuffer, size_t *cbufferLength) {
	bool ok = true;
	initCompress();
	unsigned compressBufferLength = 1024 * 16;
	u_char *compressBuffer = new FILE_LINE(0) u_char[compressBufferLength];
	zipStream->avail_in = bufferLength;
	zipStream->next_in = buffer;
	do {
		zipStream->avail_out = compressBufferLength;
		zipStream->next_out = compressBuffer;
		int deflateRslt = deflate(zipStream, Z_FINISH);
		if(deflateRslt == Z_OK || deflateRslt == Z_STREAM_END) {
			unsigned have = compressBufferLength - zipStream->avail_out;
			destBuffer->add(compressBuffer, have);
		} else {
			ok = false;
			break;
		}
	} while(this->zipStream->avail_out == 0);
	delete [] compressBuffer;
	if(destBuffer->size() && ok) {
		*cbufferLength = destBuffer->size();
		*cbuffer = new FILE_LINE(0) u_char[*cbufferLength];
		memcpy(*cbuffer, destBuffer->data(), *cbufferLength);
	} else {
		*cbuffer = NULL;
		*cbufferLength = 0;
	}
	return(ok);
}

bool cGzip::compressString(string &str, u_char **cbuffer, size_t *cbufferLength) {
	return(compress((u_char*)str.c_str(), str.length(), cbuffer, cbufferLength));
}

bool cGzip::decompress(u_char *buffer, size_t bufferLength, u_char **dbuffer, size_t *dbufferLength) {
	bool ok = true;
	initDecompress();
	unsigned decompressBufferLength = 1024 * 16;
	u_char *decompressBuffer = new FILE_LINE(0) u_char[decompressBufferLength];
	zipStream->avail_in = bufferLength;
	zipStream->next_in = buffer;
	do {
		zipStream->avail_out = decompressBufferLength;
		zipStream->next_out = decompressBuffer;
		int inflateRslt = inflate(zipStream, Z_NO_FLUSH);
		if(inflateRslt == Z_OK || inflateRslt == Z_STREAM_END) {
			int have = decompressBufferLength - zipStream->avail_out;
			destBuffer->add(decompressBuffer, have);
		} else {
			ok = false;
			break;
		}
	} while(zipStream->avail_out == 0);
	delete [] decompressBuffer;
	if(destBuffer->size() && ok) {
		*dbufferLength = destBuffer->size();
		*dbuffer = new FILE_LINE(0) u_char[*dbufferLength];
		memcpy(*dbuffer, destBuffer->data(), *dbufferLength);
	} else {
		*dbuffer = NULL;
		*dbufferLength = 0;
	}
	return(ok);
}

string cGzip::decompressString(u_char *buffer, size_t bufferLength) {
	u_char *dbuffer;
	size_t dbufferLength;
	if(decompress(buffer, bufferLength, &dbuffer, &dbufferLength)) {
		string rslt = string((char*)dbuffer, dbufferLength);
		delete [] dbuffer;
		return(rslt);
	} else {
		return("");
	}
}

bool cGzip::isCompress(u_char *buffer, size_t bufferLength) {
	return(bufferLength > 2 && buffer && buffer[0] == 0x1F && buffer[1] == 0x8B);
}

void cGzip::initCompress() {
	term();
	destBuffer = new FILE_LINE(0) SimpleBuffer;
	zipStream =  new FILE_LINE(0) z_stream;
	zipStream->zalloc = Z_NULL;
	zipStream->zfree = Z_NULL;
	zipStream->opaque = Z_NULL;
	deflateInit2(zipStream, 5, Z_DEFLATED, MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY);
	operation = _compress;
}

void cGzip::initDecompress() {
	term();
	destBuffer = new FILE_LINE(0) SimpleBuffer;
	zipStream =  new FILE_LINE(0) z_stream;
	zipStream->zalloc = Z_NULL;
	zipStream->zfree = Z_NULL;
	zipStream->opaque = Z_NULL;
	zipStream->avail_in = 0;
	zipStream->next_in = Z_NULL;
	inflateInit2(zipStream, MAX_WBITS + 16);
	operation = _decompress;
}

void cGzip::term() {
	if(zipStream) {
		switch(operation) {
		case _compress:
			deflateEnd(zipStream);
			break;
		case _decompress:
			inflateEnd(zipStream);
			break;
		case _na:
			break;
		}
		delete zipStream;
		zipStream = NULL;
	}
	if(destBuffer) {
		delete destBuffer;
		destBuffer = NULL;
	}
}


cResolver::cResolver() {
	use_lock = true;
	res_timeout = 120;
	_sync_lock = 0;
}

u_int32_t cResolver::resolve(const char *host, unsigned timeout, eTypeResolve typeResolve) {
	if(use_lock) {
		lock();
	}
	u_int32_t ipl = 0;
	time_t now = time(NULL);
	map<string, sIP_time>::iterator iter_find = res_table.find(host);
	if(iter_find != res_table.end() &&
	   (iter_find->second.timeout == UINT_MAX ||
	    iter_find->second.at + iter_find->second.timeout > now)) {
		ipl = iter_find->second.ipl;
	}
	if(!ipl) {
		if(reg_match(host, "[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+", __FILE__, __LINE__)) {
			in_addr ips;
			inet_aton(host, &ips);
			ipl = ips.s_addr;
			res_table[host].ipl = ipl;
			res_table[host].at = now;
			res_table[host].timeout = UINT_MAX;
		} else {
			if(typeResolve == _typeResolve_default) {
				#if defined(__arm__)
					typeResolve = _typeResolve_system_host;
				#else
					typeResolve = _typeResolve_std;
				#endif
			}
			if(typeResolve == _typeResolve_std) {
				ipl = resolve_std(host);
			} else if(typeResolve == _typeResolve_system_host) {
				ipl = resolve_by_system_host(host);
			}
			if(ipl) {
				res_table[host].ipl = ipl;
				res_table[host].at = now;
				res_table[host].timeout = timeout ? timeout : 120;
				syslog(LOG_NOTICE, "resolve host %s to %s", host, inet_ntostring(htonl(ipl)).c_str());
			}
		}
	}
	if(use_lock) {
		unlock();
	}
	return(ipl);
}

u_int32_t cResolver::resolve_n(const char *host, unsigned timeout, eTypeResolve typeResolve) {
	extern cResolver resolver;
	return(resolver.resolve(host, timeout, typeResolve));
}

string cResolver::resolve_str(const char *host, unsigned timeout, eTypeResolve typeResolve) {
	extern cResolver resolver;
	u_int32_t ipl = resolver.resolve(host, timeout, typeResolve);
	if(ipl) {
		return(inet_ntostring(htonl(ipl)));
	}
	return("");
}

u_int32_t cResolver::resolve_std(const char *host) {
	u_int32_t ipl = 0;
	hostent *rslt_hostent = gethostbyname(host);
	if(rslt_hostent) {
		ipl = ((in_addr*)rslt_hostent->h_addr)->s_addr;
	}
	return(ipl);
}

u_int32_t cResolver::resolve_by_system_host(const char *host) {
	u_int32_t ipl = 0;
	FILE *cmd_pipe = popen((string("host -t A ") + host + " 2>/dev/null").c_str(), "r");
	if(cmd_pipe) {
		char bufRslt[512];
		while(fgets(bufRslt, sizeof(bufRslt), cmd_pipe)) {
			string ipl_str = reg_replace(bufRslt, "([0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+)", "$1", __FILE__, __LINE__);
			if(!ipl_str.empty()) {
				ipl = inet_strington(ipl_str.c_str());
				if(ipl) {
					ipl = ntohl(ipl);
					break;
				}
			}
		}
		pclose(cmd_pipe);
	}
	return(ipl);
}


cUtfConverter::cUtfConverter() {
	cnv_utf8 = NULL;
	init_ok = false;
	_sync_lock = 0;
	init();
}

cUtfConverter::~cUtfConverter() {
	term();
}

bool cUtfConverter::check(const char *str) {
	if(!str || !*str || is_ascii(str)) {
		return(true);
	}
	bool okUtf = false;
	if(init_ok) {
		unsigned strLen = strlen(str);
		unsigned strLimit = strLen * 2 + 10;
		unsigned strUtfLimit = strLen * 2 + 10;
		UChar *strUtf = new FILE_LINE(0) UChar[strUtfLimit + 1];
		UErrorCode status = U_ZERO_ERROR;
		lock();
		ucnv_toUChars(cnv_utf8, strUtf, strUtfLimit, str, -1, &status);
		unlock();
		if(status == U_ZERO_ERROR) {
			char *str_check = new FILE_LINE(0) char[strLimit + 1];
			lock();
			ucnv_fromUChars(cnv_utf8, str_check, strLimit, strUtf, -1, &status);
			unlock();
			if(status == U_ZERO_ERROR && !strcmp(str, str_check)) {
				okUtf = true;
			}
			delete [] str_check;
		}
		delete [] strUtf;
	}
	return(okUtf);
}

string cUtfConverter::reverse(const char *str) {
	if(!str || !*str) {
		return("");
	}
	string rslt;
	bool okReverseUtf = false;
	if(init_ok && !is_ascii(str)) {
		unsigned strLen = strlen(str);
		unsigned strLimit = strLen * 2 + 10;
		unsigned strUtfLimit = strLen * 2 + 10;
		UChar *strUtf = new FILE_LINE(0) UChar[strUtfLimit + 1];
		UErrorCode status = U_ZERO_ERROR;
		lock();
		ucnv_toUChars(cnv_utf8, strUtf, strUtfLimit, str, -1, &status);
		unlock();
		if(status == U_ZERO_ERROR) {
			unsigned len = 0;
			for(unsigned i = 0; i < strUtfLimit && strUtf[i]; i++) {
				len++;
			}
			UChar *strUtf_r = new FILE_LINE(0) UChar[strUtfLimit + 1];
			for(unsigned i = 0; i < len; i++) {
				strUtf_r[len - i - 1] = strUtf[i];
			}
			strUtf_r[len] = 0;
			char *str_r = new FILE_LINE(0) char[strLimit + 1];
			lock();
			ucnv_fromUChars(cnv_utf8, str_r, strLimit, strUtf_r, -1, &status);
			unlock();
			if(status == U_ZERO_ERROR && strlen(str_r) == strLen) {
				rslt = str_r;
				okReverseUtf = true;
			}
			delete [] str_r;
			delete [] strUtf_r;
		}
		delete [] strUtf;
	}
	if(!okReverseUtf) {
		int length = strlen(str);
		for(int i = length - 1; i >= 0; i--) {
			rslt += str[i];
		}
	}
	return rslt;
}

bool cUtfConverter::is_ascii(const char *str) {
	if(!str) {
		return(true);
	}
	while(*str) {
		if((unsigned)*str > 127) {
			return(false);
		}
		++str;
	}
	return(true);
}

string cUtfConverter::remove_no_ascii(const char *str, const char subst) {
	if(!str || !*str) {
		return("");
	}
	string rslt;
	while(*str) {
		rslt += (unsigned)*str > 127 ? subst : *str;
		++str;
	}
	return(rslt);
}

void cUtfConverter::_remove_no_ascii(const char *str, const char subst) {
	if(!str) {
		return;
	}
	while(*str) {
		if((unsigned)*str > 127) {
			*(char*)str = subst;
		}
		++str;
	}
}

bool cUtfConverter::init() {
	UErrorCode status = U_ZERO_ERROR;
	cnv_utf8 = ucnv_open("utf-8", &status);
	if(status == U_ZERO_ERROR) {
		init_ok = true;
	} else {
		if(cnv_utf8) {
			ucnv_close(cnv_utf8);
		}
	}
	return(init_ok);
}

void cUtfConverter::term() {
	if(cnv_utf8) {
		ucnv_close(cnv_utf8);
	}
	init_ok = false;
}
