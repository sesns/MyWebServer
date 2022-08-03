#include "Token.h"


std::string url::decode(const std::string& text) noexcept {
    std::string out;
    for (auto it = text.begin(); it != text.end(); ++it) {
        char ch = *it;
        switch (ch) {
            case '+': {
                out += ' ';
                break;
            }
            case '%': {
                //Check by "%XX" pattern
                if (text.end() != it + 1 && text.end() != it + 2) {
                    const std::string s{it[1], it[2]}; // s is "XX"
                    out += std::stol(s, nullptr, 16); // convert hex to long int and concate with result string
                    it += 2; // skip "XX"
                }

                break;
            }
            default: {
                out += ch;
            }
        }
    }

    return out;
}


string get_hmac_sha256(const string& data_str)
{
    std::stringstream ss_result;
    std::vector<uint8_t> out(SHA256_HASH_SIZE);
    hmac_sha256(
        secret_key.data(),  secret_key.size(),
        data_str.data(), data_str.size(),
        out.data(),  out.size()
    );

    for (uint8_t x : out) {
        ss_result << std::hex << std::setfill('0') << std::setw(2) << (int)x;
    }

    return ss_result.str();
}

string base64_encode_str(const string& origin)
{
    return base64_encode(origin,false);
}

string base64_decode_str(const string& decoded_str)
{
    return base64_decode(decoded_str);
}

void Token::set_recv_token(string& recv_token)
{
    recv_token_=recv_token;
}
string Token::create_token(string& sig_method,string& uid,long long delay)
    {
        //生成原始token
        string header="alg:";
        header+=sig_method;

        string payload="";
        payload+="uid:";
        payload+=uid;
        payload+="|";

        time_t cur=time(NULL);
        long long expire=cur+delay;
        payload+="expire:";
        payload+=to_string(expire);
        payload+="|";

        payload+="issuer:";
        payload+=issuer;
        //payload+="|";

        //base64对原始token进行编码
        const string origin_header=header;
        string encoded_header=base64_encode_str(origin_header);

        const string origin_payload=payload;
        string encoded_payload=base64_encode_str(origin_payload);

        //string token_no_sig=encoded_header+"."+encoded_payload;
        string token_no_sig=encoded_header+"."+encoded_payload;

        string sig;
        if(sig_method=="hmac_sha256")
            sig=get_hmac_sha256(token_no_sig);

        string token=token_no_sig+"."+sig;
        return token;
    }

bool Token::check_token(string& token,string& res_uid)
    {
        //进行url解码
        token=url::decode(token);

        size_t first_point_pos=token.find(".");
        size_t second_point_pos=token.find(".",first_point_pos+1);

        //用base64解码header,获取签名算法
        string encoded_header=token.substr(0,first_point_pos);
        string origin_header=base64_decode_str(encoded_header);

        //根据签名算法进行验证
        string sig=token.substr(second_point_pos+1);

        string token_no_sig=token.substr(0,second_point_pos);

        if(origin_header=="alg:hmac_sha256")
        {

            string cur_sig=get_hmac_sha256(token_no_sig);

            if(sig!=cur_sig)//说明被篡改
            {
                return false;
            }

        }
        else
        {

            return false;
        }

        //解码payload
        string encoded_payload=token.substr(first_point_pos+1,second_point_pos-first_point_pos-1);
        string origin_payload=base64_decode_str(encoded_payload);

        //uid:123|expire:1314|issuer:xsy
        size_t first_shuxian=origin_payload.find("|");
        size_t second_shuxian=origin_payload.find("|",first_point_pos+1);

        string uid_=origin_payload.substr(4,first_shuxian-4);
        string expire_=origin_payload.substr(first_shuxian+8,second_shuxian-first_shuxian-8);
        string issuer_=origin_payload.substr(second_shuxian+8);
        //判断是否过期
        time_t cur=time(NULL);
        long long expire=stoll(expire_);
        if(expire<=cur)
        {
            return false;
        }

        //判断签发者是否相同
        if(issuer_!=issuer)
        {
            return false;
        }
        //验证成功
        res_uid=uid_;
        return true;

    }

bool Token::check_inner_token(string& res_uid)
{
    return check_token(recv_token_,res_uid);
}
