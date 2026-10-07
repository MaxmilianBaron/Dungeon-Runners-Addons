#include <winsock2.h>
#include <ws2tcpip.h>
#include <wincrypt.h>
#include <algorithm>
#include <map>
#include <memory>
#include <stdexcept>
#include <climits>
#include "https_client.h"
#include "windows_compat.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "https_roots.generated.h"

extern "C" int mbedtls_hardware_poll(void*,unsigned char* output,size_t size,size_t* written) {
    *written=0;HCRYPTPROV provider=0;
    if(size>MAXDWORD||!CryptAcquireContextW(&provider,nullptr,nullptr,PROV_RSA_FULL,CRYPT_VERIFYCONTEXT))return -1;
    const bool good=CryptGenRandom(provider,static_cast<DWORD>(size),output)!=FALSE;CryptReleaseContext(provider,0);if(!good)return -1;*written=size;return 0;
}
namespace AddonHttps {
namespace {
constexpr size_t BodyLimit=128*1024,HeaderLimit=16*1024;
void Require(bool value,const char* error) {if(!value)throw std::runtime_error(error);}
std::string Lower(std::string value) {for(auto& c:value)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');return value;}
size_t Number(const std::string& value,unsigned base) {
    Require(!value.empty()&&value.size()<=12&&value.find_first_not_of(base==16?"0123456789abcdefABCDEF":"0123456789")==std::string::npos,"Invalid HTTP size");size_t result=0;
    for(unsigned char c:value){const unsigned digit=c<='9'?c-'0':(c>='a'?c-'a':c-'A')+10;Require(result<=(SIZE_MAX-digit)/base,"HTTP size overflow");result=result*base+digit;}return result;
}
class Connection {
    SOCKET socket=INVALID_SOCKET;
    mbedtls_ssl_context ssl{};mbedtls_ssl_config config{};mbedtls_x509_crt roots{};mbedtls_entropy_context entropy{};mbedtls_ctr_drbg_context random{};
    bool started=false;const std::atomic<bool>& cancelled;uint64_t deadline;
    bool Active() const {return !cancelled&&WindowsCompat::Milliseconds()<deadline;}
    void Check() const {Require(Active(),"HTTPS request cancelled or timed out");}
    static int Send(void* context,const unsigned char* data,size_t size) {
        auto& self=*static_cast<Connection*>(context);if(!self.Active())return MBEDTLS_ERR_SSL_TIMEOUT;
        const int n=::send(self.socket,reinterpret_cast<const char*>(data),static_cast<int>(std::min<size_t>(size,INT_MAX)),0);return n==SOCKET_ERROR?MBEDTLS_ERR_SSL_INTERNAL_ERROR:n;
    }
    static int Receive(void* context,unsigned char* data,size_t size) {
        auto& self=*static_cast<Connection*>(context);if(!self.Active())return MBEDTLS_ERR_SSL_TIMEOUT;
        const int n=::recv(self.socket,reinterpret_cast<char*>(data),static_cast<int>(std::min<size_t>(size,INT_MAX)),0);return n==SOCKET_ERROR?MBEDTLS_ERR_SSL_TIMEOUT:n;
    }
public:
    explicit Connection(const std::atomic<bool>& stop):cancelled(stop),deadline(WindowsCompat::Milliseconds()+12000) {
        mbedtls_ssl_init(&ssl);mbedtls_ssl_config_init(&config);mbedtls_x509_crt_init(&roots);mbedtls_entropy_init(&entropy);mbedtls_ctr_drbg_init(&random);
    }
    ~Connection() {
        if(socket!=INVALID_SOCKET)closesocket(socket);mbedtls_ssl_free(&ssl);mbedtls_ssl_config_free(&config);mbedtls_x509_crt_free(&roots);mbedtls_ctr_drbg_free(&random);mbedtls_entropy_free(&entropy);if(started)WSACleanup();
    }
    void Open(const std::string& host) {
        Check();WSADATA wsa{};Require(WSAStartup(MAKEWORD(2,2),&wsa)==0,"Cannot initialize HTTPS sockets");started=true;
        addrinfo hints{},*raw=nullptr;hints.ai_family=AF_UNSPEC;hints.ai_socktype=SOCK_STREAM;Require(getaddrinfo(host.c_str(),"443",&hints,&raw)==0,"Cannot resolve HTTPS host");std::unique_ptr<addrinfo,decltype(&freeaddrinfo)> addresses(raw,freeaddrinfo);
        unsigned attempts=0;
        for(auto a=raw;a&&attempts++<8;a=a->ai_next) {
            Check();SOCKET candidate=::socket(a->ai_family,a->ai_socktype,a->ai_protocol);if(candidate==INVALID_SOCKET)continue;
            u_long nonblocking=1;int status=ioctlsocket(candidate,FIONBIO,&nonblocking)==0?::connect(candidate,a->ai_addr,static_cast<int>(a->ai_addrlen)):SOCKET_ERROR;
            if(status==SOCKET_ERROR&&WSAGetLastError()==WSAEWOULDBLOCK) {
                fd_set writable,errors;FD_ZERO(&writable);FD_ZERO(&errors);FD_SET(candidate,&writable);FD_SET(candidate,&errors);timeval timeout{2,0};
                int count=select(0,nullptr,&writable,&errors,&timeout),error=0,length=sizeof(error);status=count>0&&FD_ISSET(candidate,&writable)&&getsockopt(candidate,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&error),&length)==0&&error==0?0:SOCKET_ERROR;
            }
            if(status==0){nonblocking=0;if(ioctlsocket(candidate,FIONBIO,&nonblocking)==0){socket=candidate;break;}}closesocket(candidate);
        }
        Check();Require(socket!=INVALID_SOCKET,"Cannot connect to HTTPS host");DWORD timeout=2500;
        Require(setsockopt(socket,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout))==0&&setsockopt(socket,SOL_SOCKET,SO_SNDTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout))==0,"Cannot set HTTPS timeouts");
        const unsigned char name[]="Dungeon-Runners-Addons";
        Require(mbedtls_ctr_drbg_seed(&random,mbedtls_entropy_func,&entropy,name,sizeof(name))==0,"Secure random initialization failed");
        Require(mbedtls_x509_crt_parse(&roots,reinterpret_cast<const unsigned char*>(HttpsRoots),sizeof(HttpsRoots))==0,"Invalid HTTPS certificate store");
        Require(mbedtls_ssl_config_defaults(&config,MBEDTLS_SSL_IS_CLIENT,MBEDTLS_SSL_TRANSPORT_STREAM,MBEDTLS_SSL_PRESET_DEFAULT)==0,"Cannot initialize TLS");
        mbedtls_ssl_conf_min_tls_version(&config,MBEDTLS_SSL_VERSION_TLS1_2);mbedtls_ssl_conf_authmode(&config,MBEDTLS_SSL_VERIFY_REQUIRED);mbedtls_ssl_conf_ca_chain(&config,&roots,nullptr);mbedtls_ssl_conf_rng(&config,mbedtls_ctr_drbg_random,&random);
        Require(mbedtls_ssl_setup(&ssl,&config)==0&&mbedtls_ssl_set_hostname(&ssl,host.c_str())==0,"Cannot configure TLS hostname");mbedtls_ssl_set_bio(&ssl,this,Send,Receive,nullptr);
        int code;do{Check();code=mbedtls_ssl_handshake(&ssl);}while(code==MBEDTLS_ERR_SSL_WANT_READ||code==MBEDTLS_ERR_SSL_WANT_WRITE);
        Require(mbedtls_ssl_get_verify_result(&ssl)==0,"HTTPS certificate verification failed");Require(code==0,"TLS connection failed");
    }
    std::string Request(const std::string& host,const std::string& path) {
        const std::string request="GET "+path+" HTTP/1.1\r\nHost: "+host+"\r\nUser-Agent: Dungeon-Runners-Addons/Leaderboard\r\nAccept: application/json\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n";
        size_t position=0;while(position<request.size()){Check();int n=mbedtls_ssl_write(&ssl,reinterpret_cast<const unsigned char*>(request.data()+position),request.size()-position);Require(n>0,"HTTPS send failed");position+=n;}
        std::string response;unsigned char buffer[8192];
        for(;;){Check();int n=mbedtls_ssl_read(&ssl,buffer,sizeof(buffer));if(n==0||n==MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)break;Require(n>0,"HTTPS response was interrupted");Require(response.size()+n<=BodyLimit*4+HeaderLimit,"HTTPS response is too large");response.append(reinterpret_cast<const char*>(buffer),n);}return response;
    }
};
}
Response Parse(const std::string& response) {
    size_t position=0;auto line=[&](){const size_t end=response.find("\r\n",position);Require(end!=std::string::npos&&end-position<=HeaderLimit,"Incomplete HTTP header");auto value=response.substr(position,end-position);position=end+2;return value;};
    const std::string status=line();Require(status.size()>=12&&(status.compare(0,9,"HTTP/1.1 ")==0||status.compare(0,9,"HTTP/1.0 ")==0),"Invalid HTTP status");Response result;result.status=static_cast<unsigned>(Number(status.substr(9,3),10));
    std::map<std::string,std::string> headers;
    for(;;){auto value=line();Require(position<=HeaderLimit,"HTTP headers are too large");if(value.empty())break;const auto colon=value.find(':');Require(colon!=std::string::npos&&colon>0,"Invalid HTTP header");auto key=Lower(value.substr(0,colon));value.erase(0,colon+1);const auto first=value.find_first_not_of(" \t");value=first==std::string::npos?"":value.substr(first);const auto last=value.find_last_not_of(" \t");if(last!=std::string::npos)value.resize(last+1);
        if(key=="content-length"||key=="transfer-encoding"||key=="content-type"||key=="content-encoding"||key=="retry-after")Require(headers.emplace(key,value).second,"Duplicate HTTP framing header");}
    result.contentType=Lower(headers["content-type"]);result.retryAfter=headers["retry-after"];
    Require(!headers.count("content-encoding")||Lower(headers.at("content-encoding"))=="identity","Unexpected compressed HTTP response");
    if(headers.count("transfer-encoding")) {
        Require(Lower(headers.at("transfer-encoding"))=="chunked"&&!headers.count("content-length"),"Invalid HTTP framing");
        for(;;){auto chunk=line();const auto count=Number(chunk.substr(0,chunk.find(';')),16);Require(count<=BodyLimit-result.body.size()&&count<=response.size()-position,"Invalid HTTP chunk size");
            if(!count){size_t trailer=position;while(!line().empty())Require(position-trailer<=HeaderLimit,"HTTP trailers are too large");break;}
            result.body.append(response,position,count);position+=count;Require(line().empty(),"Invalid HTTP chunk terminator");}
        Require(position==response.size(),"Unexpected trailing HTTP data");
    } else {
        const auto size=response.size()-position;Require(size<=BodyLimit&&(!headers.count("content-length")||Number(headers.at("content-length"),10)==size),"Truncated or oversized HTTP body");result.body=response.substr(position);
    }
    return result;
}
Response Get(const std::string& host,const std::string& path,const std::atomic<bool>& cancelled) {
    Require(!host.empty()&&host.size()<254&&host.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789.-")==std::string::npos&&!path.empty()&&path.front()=='/'&&path.size()<1024&&path.find_first_of("\r\n\\ ")==std::string::npos&&path.find('\0')==std::string::npos,"Invalid HTTPS endpoint");
    Connection connection(cancelled);connection.Open(host);return Parse(connection.Request(host,path));
}
}
