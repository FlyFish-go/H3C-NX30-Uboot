#include "official_api.h"
#include <limits.h>
#include <string.h>

typedef struct { const unsigned char *s; size_t n,p,fields,values; oa_response *r; unsigned seen; } parser;
typedef struct { unsigned char text[OA_MAX_KEY+1u]; size_t n; } key;
typedef struct { int kind; int64_t integer; size_t n; unsigned char text[OA_MAX_STRING+1u]; } value;
enum { V_OTHER=0,V_STRING=1,V_INTEGER=2,V_FLOAT=3 };
static int hex(unsigned c){if(c>='0'&&c<='9')return (int)(c-'0');if(c>='a'&&c<='f')return (int)(c-'a'+10);if(c>='A'&&c<='F')return (int)(c-'A'+10);return -1;}
static size_t bounded(const char*s,size_t max){size_t n=0;while(n<max&&s[n])n++;return n;}
static int ascii_equal(const char*a,const char*b,size_t n){for(size_t i=0;i<n;i++){unsigned x=(unsigned char)a[i],y=(unsigned char)b[i];if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;if(x!=y)return 0;}return 1;}
/* Return one canonical scalar and its encoded size, never read past n. */
static oa_code scalar(const unsigned char*s,size_t n,uint32_t*cp,size_t*z){
    unsigned a,b,c,d;if(!n)return OA_UTF8;a=s[0];
    if(a<128){*cp=a;*z=1;return OA_OK;}
    if(a>=0xc2&&a<=0xdf){if(n<2)return OA_UTF8;b=s[1];if((b&0xc0)!=0x80)return OA_UTF8;*cp=((a&31)<<6)|(b&63);*z=2;return OA_OK;}
    if(a>=0xe0&&a<=0xef){if(n<3)return OA_UTF8;b=s[1];c=s[2];if((b&0xc0)!=0x80||(c&0xc0)!=0x80||(a==0xe0&&b<0xa0)||(a==0xed&&b>=0xa0))return OA_UTF8;*cp=((a&15)<<12)|((b&63)<<6)|(c&63);*z=3;return OA_OK;}
    if(a>=0xf0&&a<=0xf4){if(n<4)return OA_UTF8;b=s[1];c=s[2];d=s[3];if((b&0xc0)!=0x80||(c&0xc0)!=0x80||(d&0xc0)!=0x80||(a==0xf0&&b<0x90)||(a==0xf4&&b>=0x90))return OA_UTF8;*cp=((a&7)<<18)|((b&63)<<12)|((c&63)<<6)|(d&63);*z=4;return OA_OK;}
    return OA_UTF8;
}
static void ws(parser*p){while(p->p<p->n&&(p->s[p->p]==' '||p->s[p->p]=='\t'||p->s[p->p]=='\n'||p->s[p->p]=='\r'))p->p++;}
static oa_code u16(parser*p,uint32_t*out){uint32_t c=0;if(p->n-p->p<4)return OA_JSON;for(unsigned i=0;i<4;i++){int h=hex(p->s[p->p++]);if(h<0)return OA_JSON;c=(c<<4)|(unsigned)h;}*out=c;return OA_OK;}
static oa_code string(parser*p,unsigned char*out,size_t cap,size_t*len){
    size_t n=0;if(p->p==p->n||p->s[p->p++]!='"')return OA_JSON;
    while(p->p<p->n){unsigned char b=p->s[p->p++];unsigned char encoded[4];size_t z=1;uint32_t cp=0;oa_code rc;
        if(b=='"'){out[n]=0;*len=n;return OA_OK;}
        if(b<32)return OA_JSON;
        if(b=='\\'){
            if(p->p==p->n)return OA_JSON;
            b=p->s[p->p++];
            switch(b){case '"':case '\\':case '/':encoded[0]=b;break;case 'b':encoded[0]=8;break;case 'f':encoded[0]=12;break;case 'n':encoded[0]=10;break;case 'r':encoded[0]=13;break;case 't':encoded[0]=9;break;
            case 'u':
                rc=u16(p,&cp);if(rc)return rc;
                if(cp>=0xd800&&cp<=0xdbff){uint32_t low;if(p->n-p->p<6||p->s[p->p]!='\\'||p->s[p->p+1]!='u')return OA_JSON;p->p+=2;rc=u16(p,&low);if(rc)return rc;if(low<0xdc00||low>0xdfff)return OA_JSON;cp=0x10000+((cp-0xd800)<<10)+(low-0xdc00);}
                else if(cp>=0xdc00&&cp<=0xdfff)return OA_JSON;
                if(cp<0x80){encoded[0]=(unsigned char)cp;z=1;}
                else if(cp<0x800){encoded[0]=(unsigned char)(0xc0|(cp>>6));encoded[1]=(unsigned char)(0x80|(cp&63));z=2;}
                else if(cp<0x10000){encoded[0]=(unsigned char)(0xe0|(cp>>12));encoded[1]=(unsigned char)(0x80|((cp>>6)&63));encoded[2]=(unsigned char)(0x80|(cp&63));z=3;}
                else{encoded[0]=(unsigned char)(0xf0|(cp>>18));encoded[1]=(unsigned char)(0x80|((cp>>12)&63));encoded[2]=(unsigned char)(0x80|((cp>>6)&63));encoded[3]=(unsigned char)(0x80|(cp&63));z=4;}break;
            default:return OA_JSON;}
        }else if(b>=128){p->p--;rc=scalar(p->s+p->p,p->n-p->p,&cp,&z);if(rc)return rc;memcpy(encoded,p->s+p->p,z);p->p+=z;}
        else encoded[0]=b;
        if(n+z>=cap)return OA_LIMIT;
        memcpy(out+n,encoded,z);n+=z;
    }
    return OA_JSON;
}
/* Grammar is locale independent. Decimal comparison rejects floats beyond
 * exact DBL_MAX; tiny decimal values are valid ignored JSON metadata. */
static oa_code number(parser*p,value*v){
    static const char maxdouble[]="179769313486231570814527423731704356798070567525844996598917476803157260780028538760589558632766878171540458953514382464234321326889464182768467546703537516986049910576551282076245490090389328944075868508455133942304583236903222948165808559332123348274797826204144723168738177180919299881250404026184124858368";
    size_t start=p->p,mant,end,point,first;int neg=0,isfloat=0,exp=0,exneg=0;uint64_t u=0,limit;
    if(p->s[p->p]=='-'){neg=1;p->p++;if(p->p==p->n)return OA_JSON;}
    mant=p->p;if(p->s[p->p]=='0'){p->p++;if(p->p<p->n&&p->s[p->p]>='0'&&p->s[p->p]<='9')return OA_JSON;}
    else {if(p->s[p->p]<'1'||p->s[p->p]>'9')return OA_JSON;while(p->p<p->n&&p->s[p->p]>='0'&&p->s[p->p]<='9')p->p++;}
    point=p->p;
    if(p->p<p->n&&p->s[p->p]=='.'){isfloat=1;p->p++;size_t digit=p->p;while(p->p<p->n&&p->s[p->p]>='0'&&p->s[p->p]<='9')p->p++;if(p->p==digit)return OA_JSON;}
    end=p->p;
    if(p->p<p->n&&(p->s[p->p]=='e'||p->s[p->p]=='E')){isfloat=1;p->p++;if(p->p<p->n&&(p->s[p->p]=='+'||p->s[p->p]=='-'))exneg=p->s[p->p++]=='-';size_t digit=p->p;while(p->p<p->n&&p->s[p->p]>='0'&&p->s[p->p]<='9'){if(exp>10000)return OA_NUMBER_RANGE;exp=exp*10+(p->s[p->p++]-'0');}if(p->p==digit)return OA_JSON;if(exp>10000)return OA_NUMBER_RANGE;}
    if(p->p-start>127)return OA_LIMIT;
    if(!isfloat){limit=neg?UINT64_C(9223372036854775808):UINT64_C(9223372036854775807);for(size_t i=mant;i<end;i++){unsigned d=p->s[i]-'0';if(u>(limit-d)/10)return OA_NUMBER_RANGE;u=u*10+d;}v->kind=V_INTEGER;v->integer=neg?(u==UINT64_C(9223372036854775808)?INT64_MIN:-(int64_t)u):(int64_t)u;return OA_OK;}
    first=mant;while(first<end&&(p->s[first]=='0'||p->s[first]=='.'))first++;
    if(first<end){int decimal_exp=(exneg?-exp:exp)+(int)(point-first)-(first<point?1:0);if(decimal_exp>308)return OA_NUMBER_RANGE;
        if(decimal_exp==308){size_t pos=first;for(size_t i=0;i<sizeof(maxdouble)-1;i++){while(pos<end&&p->s[pos]=='.')pos++;unsigned d=pos<end?p->s[pos++]:'0';if(d>(unsigned char)maxdouble[i])return OA_NUMBER_RANGE;if(d<(unsigned char)maxdouble[i])break;}}
    }
    v->kind=V_FLOAT;return OA_OK;
}
static int keyis(const key*k,const char*s){size_t n=strlen(s);return k->n==n&&!memcmp(k->text,s,n);}
static int clean(const unsigned char*s,size_t n){size_t i=0,z;uint32_t cp;while(i<n){if(scalar(s+i,n-i,&cp,&z)!=OA_OK||cp<32||(cp>=127&&cp<=159))return 0;i+=z;}return 1;}
static oa_code root_field(parser*p,const key*k,const value*v){
    unsigned bit=0;
    if(keyis(k,"retCode")){bit=1;if(v->kind!=V_INTEGER)return OA_SCHEMA;p->r->ret_code=v->integer;}
    else if(keyis(k,"gwBestVer")){bit=2;if(v->kind!=V_STRING||!v->n||v->n>OA_MAX_VERSION||!clean(v->text,v->n))return OA_SCHEMA;memcpy(p->r->version,v->text,v->n+1);}
    else if(keyis(k,"VerURL")){bit=4;if(v->kind!=V_STRING||!v->n||!clean(v->text,v->n))return OA_SCHEMA;memcpy(p->r->download_url,v->text,v->n+1);}
    else if(keyis(k,"FileSize")){
        uint64_t size=0;bit=8;
        if(v->kind==V_INTEGER){if(v->integer<=0||v->integer>OA_MAX_DOWNLOAD)return OA_SCHEMA;size=(uint64_t)v->integer;}
        else if(v->kind==V_STRING){if(!v->n){p->seen|=bit;return OA_OK;}for(size_t i=0;i<v->n;i++){unsigned c=v->text[i];if(c<'0'||c>'9')return OA_SCHEMA;unsigned d=c-'0';if(size>(OA_MAX_DOWNLOAD-d)/10u)return OA_SCHEMA;size=size*10+d;}if(!size)return OA_SCHEMA;}
        else return OA_SCHEMA;
        p->r->has_file_size=1;p->r->file_size=size;
    }else if(!keyis(k,"NextAskTime")&&!keyis(k,"RebootTime")&&!keyis(k,"RebootType"))p->r->has_unknown_fields=1;
    p->seen|=bit;return OA_OK;
}
static oa_code parse_value(parser*,unsigned,value*);
static oa_code object(parser*p,unsigned depth,int root){
    key keys[OA_MAX_FIELDS];size_t count=0;oa_code rc;value v;
    p->p++;ws(p);if(p->p<p->n&&p->s[p->p]=='}'){p->p++;return OA_OK;}
    for(;;){if(count==OA_MAX_FIELDS||++p->fields>OA_MAX_TOTAL_FIELDS)return OA_LIMIT;
        rc=string(p,keys[count].text,sizeof(keys[count].text),&keys[count].n);if(rc)return rc;
        for(size_t i=0;i<count;i++)if(keys[i].n==keys[count].n&&!memcmp(keys[i].text,keys[count].text,keys[i].n))return OA_DUPLICATE_KEY;
        ws(p);if(p->p==p->n||p->s[p->p++]!=':')return OA_JSON;ws(p);rc=parse_value(p,depth+1,&v);if(rc)return rc;
        if(root){rc=root_field(p,&keys[count],&v);if(rc)return rc;}count++;
        ws(p);if(p->p==p->n)return OA_JSON;if(p->s[p->p]=='}'){p->p++;return OA_OK;}if(p->s[p->p++]!=',')return OA_JSON;ws(p);
    }
}
static oa_code parse_value(parser*p,unsigned depth,value*v){
    oa_code rc;if(depth>OA_MAX_DEPTH||++p->values>OA_MAX_VALUES)return OA_LIMIT;
    if(p->p==p->n)return OA_JSON;
    v->kind=V_OTHER;v->n=0;
    switch(p->s[p->p]){
    case '{':return object(p,depth,0);
    case '[':p->p++;ws(p);if(p->p<p->n&&p->s[p->p]==']'){p->p++;return OA_OK;}for(;;){rc=parse_value(p,depth+1,v);if(rc)return rc;ws(p);if(p->p==p->n)return OA_JSON;if(p->s[p->p]==']'){p->p++;v->kind=V_OTHER;return OA_OK;}if(p->s[p->p++]!=',')return OA_JSON;ws(p);}
    case '"':v->kind=V_STRING;return string(p,v->text,sizeof(v->text),&v->n);
    case 't':if(p->n-p->p>=4&&!memcmp(p->s+p->p,"true",4)){p->p+=4;return OA_OK;}return OA_JSON;
    case 'f':if(p->n-p->p>=5&&!memcmp(p->s+p->p,"false",5)){p->p+=5;return OA_OK;}return OA_JSON;
    case 'n':if(p->n-p->p>=4&&!memcmp(p->s+p->p,"null",4)){p->p+=4;return OA_OK;}return OA_JSON;
    default:if(p->s[p->p]=='-'||(p->s[p->p]>='0'&&p->s[p->p]<='9'))return number(p,v);return OA_JSON;
    }
}
static int control_or_bad_url(const char*s,size_t n){for(size_t i=0;i<n;i++){unsigned c=(unsigned char)s[i];if(c<=32||c>=127||c=='\\'||c=='#'||strchr("\"<>^`{|}",(int)c))return 1;if(c=='%'){if(i+2>=n||hex((unsigned char)s[i+1])<0||hex((unsigned char)s[i+2])<0)return 1;}}return 0;}
static int double_separator(const char*s,size_t n){
    for(size_t i=0;i+4<n;i++)if(s[i]=='%'&&s[i+1]=='2'&&s[i+2]=='5'){
        size_t j=i+3;while(j+1<n&&s[j]=='2'&&s[j+1]=='5')j+=2;
        if(j+1<n&&((s[j]=='2'&&s[j+1]=='6')||(s[j]=='3'&&(s[j+1]=='d'||s[j+1]=='D'))))return 1;
    }
    return 0;
}
oa_code oa_secure_download_url(const char*raw,char*out,size_t cap){
    static const char host[]="h3c-magic.oss-cn-hangzhou.aliyuncs.com";
    size_t n,scheme,slash,q;oa_code rc=OA_URL;
    if(out&&cap)memset(out,0,cap);
    if(!raw||!out||!cap)return OA_ARGUMENT;
    n=bounded(raw,OA_MAX_STRING+1u);if(n>OA_MAX_STRING||!n)return OA_LIMIT;
    if(n>=7&&ascii_equal(raw,"http://",7))scheme=7;
    else if(n>=8&&ascii_equal(raw,"https://",8))scheme=8;
    else return OA_URL;
    /* Accepted output must remain valid when the transport validates it again. */
    if(8u+n-scheme>OA_MAX_STRING)return OA_LIMIT;
    slash=scheme;while(slash<n&&raw[slash]!='/'&&raw[slash]!='?')slash++;
    if(slash-scheme!=sizeof(host)-1||!ascii_equal(raw+scheme,host,sizeof(host)-1)||slash==n||raw[slash]!='/')return OA_URL;
    q=slash;while(q<n&&raw[q]!='?')q++;
    if(q==n||q+1==n)return OA_URL;
    if(control_or_bad_url(raw,n)||double_separator(raw,n))return OA_URL;
    for(size_t i=0;i+5<=n;i++)if(!memcmp(raw+i,"&amp;",5))return OA_URL;
    /* Deliberately leave host case and raw path/query byte-for-byte intact. */
    if(8u+(n-scheme)+1u>cap)rc=OA_BUFFER;
    else{memcpy(out,"https://",8);memcpy(out+8,raw+scheme,n-scheme+1);rc=OA_OK;}
    return rc;
}
oa_code oa_parse_response(const void*json,size_t len,oa_response*out){
    oa_response r;parser p;oa_code rc;char url[OA_MAX_STRING+2u];
    if(!out)return OA_ARGUMENT;
    memset(out,0,sizeof(*out));if(!json)return OA_ARGUMENT;if(len>OA_MAX_RESPONSE)return OA_LIMIT;
    memset(&r,0,sizeof(r));memset(&p,0,sizeof(p));p.s=json;p.n=len;p.r=&r;
    ws(&p);if(p.p==len||p.s[p.p]!='{')return OA_JSON;
    rc=object(&p,0,1);if(rc)return rc;ws(&p);if(p.p!=len)return OA_JSON;
    if(!(p.seen&1u))return OA_SCHEMA;
    if(r.ret_code!=0){out->ret_code=r.ret_code;out->has_unknown_fields=r.has_unknown_fields;return OA_SERVER_REJECTED;}
    if((p.seen&6u)!=6u)return OA_SCHEMA;
    rc=oa_secure_download_url(r.download_url,url,sizeof(url));if(rc)return rc;
    memcpy(r.download_url,url,strlen(url)+1);*out=r;return OA_OK;
}
static oa_code input_text(const char*s,size_t max,size_t*n){
    size_t i=0;uint32_t cp;size_t z;oa_code rc;if(!s)return OA_ARGUMENT;*n=bounded(s,max+1);if(!*n||*n>max)return OA_SCHEMA;
    while(i<*n){rc=scalar((const unsigned char*)s+i,*n-i,&cp,&z);if(rc)return rc;if(cp<32||(cp>=127&&cp<=159))return OA_SCHEMA;i+=z;}
    return OA_OK;
}
static size_t escaped_size(const char*s,size_t n){size_t z=n;for(size_t i=0;i<n;i++)if(s[i]=='"'||s[i]=='\\')z++;return z;}
static size_t append_escaped(char*out,size_t at,const char*s,size_t n){for(size_t i=0;i<n;i++){if(s[i]=='"'||s[i]=='\\')out[at++]='\\';out[at++]=s[i];}return at;}
oa_code oa_encode_request(const char*version,const char*serial,uint32_t ask,char*out,size_t cap,size_t*written){
    static const char a[]="{\"gwVer\":\"",b[]="\",\"gwSn\":\"",c[]="\",\"gwAskCount\":";
    size_t vn,sn,need,at=0;char digits[10];size_t count=0;uint32_t x=ask;oa_code rc;
    if(written)*written=0;
    if(out&&cap)memset(out,0,cap);
    if(!out||!cap||!written)return OA_ARGUMENT;
    rc=input_text(version,OA_MAX_VERSION,&vn);if(rc)return rc;
    if(!serial||!*serial)return OA_IDENTITY_REQUIRED;
    rc=input_text(serial,OA_MAX_IDENTITY,&sn);if(rc)return rc;
    if(ask>1000000u)return OA_SCHEMA;
    do{digits[count++]=(char)('0'+x%10);x/=10;}while(x);
    need=sizeof(a)-1+escaped_size(version,vn)+sizeof(b)-1+escaped_size(serial,sn)+sizeof(c)-1+count+1;
    if(need+1>cap)return OA_BUFFER;
    memcpy(out+at,a,sizeof(a)-1);at+=sizeof(a)-1;at=append_escaped(out,at,version,vn);
    memcpy(out+at,b,sizeof(b)-1);at+=sizeof(b)-1;at=append_escaped(out,at,serial,sn);
    memcpy(out+at,c,sizeof(c)-1);at+=sizeof(c)-1;for(size_t i=count;i>0;i--)out[at++]=digits[i-1];out[at++]='}';out[at]=0;*written=at;return OA_OK;
}
oa_code oa_check_redirect(unsigned status){return status>=300&&status<400?OA_REDIRECT_REJECTED:OA_OK;}
