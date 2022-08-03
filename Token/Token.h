#ifndef TOKEN_H_INCLUDED
#define TOKEN_H_INCLUDED

#include<iostream>
#include<string>
#include <sstream>
#include<vector>
#include<iomanip>
#include "base64.h"
#include "hmac_sha256.h"
using namespace std;

#define SHA256_HASH_SIZE 32

namespace url {
    /**
     * Decode URL
     *
     * @param text encoded URL
     *
     * @return decoded URL
     */
    std::string decode(const std::string& text) noexcept;
};

const string secret_key="xsy-super-secret-key";
const string issuer="xsy";

class Token
{
public:
    Token()
    {

    }
    ~Token()
    {

    }

    string create_token(string& sig_method,string& uid,long long delay);
    bool check_token(string& token,string& res_uid);
    bool check_inner_token(string& res_uid);
    void set_recv_token(string& recv_token);

private:
    string recv_token_;

};

#endif // TOKEN_H_INCLUDED
