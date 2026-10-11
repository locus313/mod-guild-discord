/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 */

#include "WebhookClient.h"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = net::ssl;

namespace GuildDiscord
{
    PostResult PostWebhook(WebhookTarget const& target, std::string const& jsonPayload)
    {
        PostResult result;

        try
        {
            net::io_context ioc;
            ssl::context ctx(ssl::context::tls_client);
            ctx.set_default_verify_paths();
            ctx.set_verify_mode(ssl::verify_peer);

            net::ip::tcp::resolver resolver(ioc);
            beast::ssl_stream<beast::tcp_stream> stream(ioc, ctx);

            if (!SSL_set_tlsext_host_name(stream.native_handle(), target.host.c_str()))
                throw beast::system_error(beast::error_code(static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()));
            stream.set_verify_callback(ssl::host_name_verification(target.host));

            beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(10));
            beast::get_lowest_layer(stream).connect(resolver.resolve(target.host, "443"));
            stream.handshake(ssl::stream_base::client);

            http::request<http::string_body> req{ http::verb::post, target.target, 11 };
            req.set(http::field::host, target.host);
            req.set(http::field::user_agent, "mod-guild-discord");
            req.set(http::field::content_type, "application/json");
            req.set(http::field::connection, "close");
            req.body() = jsonPayload;
            req.prepare_payload();

            beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(10));
            http::write(stream, req);

            beast::flat_buffer buffer;
            http::response<http::string_body> res;
            http::read(stream, buffer, res);

            result.status = res.result_int();
            result.rateLimit.retryAfter = std::string(res["Retry-After"]);
            result.rateLimit.remaining = std::string(res["X-RateLimit-Remaining"]);
            result.rateLimit.resetAfter = std::string(res["X-RateLimit-Reset-After"]);

            beast::error_code ec;
            stream.shutdown(ec); // Discord closes first; shutdown errors are irrelevant after a full response
        }
        catch (std::exception const& e)
        {
            result.status = 0;
            result.error = e.what();
        }

        return result;
    }
}
