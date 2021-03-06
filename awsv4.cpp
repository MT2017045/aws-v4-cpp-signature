#include "awsv4.hpp"

namespace AWSV4 {

    const std::string join(const std::vector<std::string>& ss,const std::string delim) noexcept {
        std::stringstream sstream;
        const auto l = ss.size() - 1;
        std::vector<int>::size_type i;
        for (i = 0; i < l; i++) {
            sstream << ss.at(i) << delim;
        }
        sstream << ss.back();
        return sstream.str();
    }

    // http://stackoverflow.com/questions/2262386/generate-sha256-with-openssl-and-c
    void sha256(const std::string str, unsigned char outputBuffer[SHA256_DIGEST_LENGTH]) noexcept {
        char *c_string = new char [str.length()+1];
        std::strcpy(c_string, str.c_str());        
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256_CTX sha256;
        SHA256_Init(&sha256);
        SHA256_Update(&sha256, c_string, strlen(c_string));
        SHA256_Final(hash, &sha256);
        for (int i=0;i<SHA256_DIGEST_LENGTH;i++) {
            outputBuffer[i] = hash[i];
        }
    }
    
    const std::string sha256_base16(const std::string str) noexcept { 
        unsigned char hashOut[SHA256_DIGEST_LENGTH];
        AWSV4::sha256(str,hashOut);
        char outputBuffer[65];
        for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
            sprintf(outputBuffer + (i * 2), "%02x", hashOut[i]);
        }
        outputBuffer[64] = 0;
        return std::string{outputBuffer};
    }

    // -----------------------------------------------------------------------------------
    // TASK 1 - create a canonical request
    // http://docs.aws.amazon.com/general/latest/gr/sigv4-create-canonical-request.html

    // uri should be normalize()'d before calling here, as this takes a const ref param and we don't 
    // want to normalize repeatedly. the return value is not a uri specifically, but a uri fragment,
    // as such the return value should not be used to initialize a uri object
    const std::string canonicalize_uri(const Poco::URI& uri) noexcept {
        const auto p = uri.getPath();
        if (p.empty()) return "/";
        std::string encoded_path;
        Poco::URI::encode(uri.getPath(),"",encoded_path);
        return encoded_path;
    }

    const std::string canonicalize_query(const Poco::URI& uri) noexcept {
        const std::string query_delim{"&"};
        const auto q = uri.getQuery();
        if (q.empty()) return "";
        const Poco::StringTokenizer tok{q,query_delim,0};
        std::vector<std::string> parts; 
        for (const auto& t:tok) {
            std::string encoded_arg;
            Poco::URI::encode(t,"",encoded_arg);
            parts.push_back(encoded_arg);
        }
        std::sort(parts.begin(),parts.end());
        return join(parts,query_delim);
    }

    // create a map of the "canonicalized" headers
    // will return empty map on malformed input.
    const std::map<std::string,std::string> canonicalize_headers(const std::vector<std::string>& headers) noexcept {
        const std::string header_delim{":"};
        std::map<std::string,std::string> header_key2val;
        for (const auto& h:headers) {
            const Poco::StringTokenizer pair{h,header_delim,2}; // 2 -> TOK_TRIM, trim whitespace
            if (pair.count() != 2) { 
                std::cerr << "malformed header: " << h << std::endl;
                header_key2val.clear();
                return header_key2val;
            }
            std::string key{pair[0]};
            const std::string val{pair[1]};
            if (key.empty() || val.empty()) {
                std::cerr << "malformed header: " << h << std::endl;
                header_key2val.clear();
                return header_key2val;
            }
            std::transform(key.begin(), key.end(), key.begin(),::tolower);
            header_key2val[key] = val;
        }
        return header_key2val;
    }

    // get a string representation of header:value lines
    const std::string map_headers_string(const std::map<std::string,std::string>& header_key2val) noexcept {
        const std::string pair_delim{":"};
        std::string h;
        for (const auto& kv:header_key2val) {
            h.append(kv.first + pair_delim + kv.second + ENDL);
        }
        return h;
    }

    // get a string representation of the header names
    const std::string map_signed_headers(const std::map<std::string,std::string>& header_key2val) noexcept {
        const std::string signed_headers_delim{";"};
        std::vector<std::string> ks;
        for (const auto& kv:header_key2val) {
            ks.push_back(kv.first);
        }
        return join(ks,signed_headers_delim);
    }

    const std::string canonicalize_request(const std::string& http_request_method,
                                           const std::string& canonical_uri,
                                           const std::string& canonical_query_string,
                                           const std::string& canonical_headers,
                                           const std::string& signed_headers,
                                           const std::string& payload) noexcept {
        return http_request_method + ENDL + 
            canonical_uri + ENDL +
            canonical_query_string + ENDL + 
            canonical_headers + ENDL + 
            signed_headers + ENDL +
            sha256_base16(payload);
    }

    // -----------------------------------------------------------------------------------
    // TASK 2 - create a string-to-sign
    // http://docs.aws.amazon.com/general/latest/gr/sigv4-create-string-to-sign.html

    const std::string string_to_sign(const std::string& algorithm,
                                     const std::time_t& request_date,
                                     const std::string& credential_scope,
                                     const std::string& hashed_canonical_request) noexcept {
        return algorithm + ENDL + 
            ISO8601_date(request_date) + ENDL +
            credential_scope + ENDL + 
            hashed_canonical_request;
    }

    const std::string credential_scope(const std::time_t& request_date, 
                                       const std::string region,
                                       const std::string service,const std::string s) noexcept {
        //const std::string s{"%2F"};
        return utc_yyyymmdd(request_date) + s + region + s + service + s + AWS4_REQUEST; 
    }

    // time_t -> 20131222T043039Z
    const std::string ISO8601_date(const std::time_t& t) noexcept {
        char buf[sizeof "20111008T070709Z"];
        std::strftime(buf, sizeof buf, "%Y%m%dT%H%M%SZ", std::gmtime(&t));
       	return std::string{buf};
    }

    // time_t -> 20131222
    const std::string utc_yyyymmdd(const std::time_t& t) noexcept {
        char buf[sizeof "20111008"];
        std::strftime(buf, sizeof buf, "%Y%m%d", std::gmtime(&t));
       	return std::string{buf};
    }
    
    // -----------------------------------------------------------------------------------
    // TASK 3
    // http://docs.aws.amazon.com/general/latest/gr/sigv4-calculate-signature.html


    const std::string sign(const std::string key, const std::string msg)
    {
        unsigned char *c_key = new unsigned char[key.length() + 1];
        memcpy(c_key, (unsigned char *)key.data(), key.length());

        unsigned char *c_msg = new unsigned char[msg.length() + 1];
        memcpy(c_msg, (unsigned char *)msg.data(), msg.length());

        unsigned char * digest = HMAC(EVP_sha256(), (unsigned char*)c_key, key.length(), c_msg, msg.length(), NULL, NULL); 

        delete[] c_key;
        delete[] c_msg;

        std::string signed_str = std::string((char *)digest, 32);
        
        return signed_str;
    }

    const std::string calculate_signature(const std::time_t& request_date, 
                                          const std::string secret,
                                          const std::string region,
                                          const std::string service,
                                          const std::string string_to_sign) noexcept {
        auto m_datestamp = utc_yyyymmdd(request_date);
	std::string kDate = sign("AWS4" + secret, m_datestamp);
        std::string kRegion = sign(kDate, region);
        std::string kService = sign(kRegion, service);
        std::string kSigning = sign(kService, "aws4_request");
	std::string signature_str = sign(kSigning, string_to_sign);

        unsigned char *signature_data = new unsigned char[signature_str.length() + 1];
        memcpy(signature_data, (unsigned char *)signature_str.data(), signature_str.length());
	unsigned char *digest=signature_data;
	char outputBuffer[65];

        for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
            sprintf(outputBuffer + (i * 2), "%02x", digest[i]);
        }
        outputBuffer[64] = 0;

        return std::string(outputBuffer);
}
}      
