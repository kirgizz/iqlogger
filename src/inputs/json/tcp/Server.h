//
// Server.h
// ~~~~~~~~~~~~~~~~
//
// Copyright (C) 2018 IQOption Software, Inc.
//
//

#pragma once

#include <array>
#include <boost/asio.hpp>
#include <iostream>

#include "MessageQueue.h"
#include "formats/json/JsonException.h"
#include "inputs/Record.h"
#include "inputs/json/Json.h"

using namespace iqlogger::inputs;
using namespace iqlogger::inputs::json;
using namespace iqlogger::formats::json;

namespace iqlogger::inputs::json::tcp {

    using boost::asio::ip::tcp;

    class Server
    {
        class tcp_connection : public std::enable_shared_from_this<tcp_connection>
        {
        public:

            using pointer = std::shared_ptr<tcp_connection>;

            static pointer create(boost::asio::io_service &io_service, RecordQueuePtr<Json> queuePtr) {
                return pointer(new tcp_connection(io_service, queuePtr));
            }

            tcp::socket &socket() {
                return m_socket;
            }

            void start()
            {
                do_read();
            }

        private:

            tcp_connection(boost::asio::io_service &io_service, RecordQueuePtr<Json> queuePtr)
                    : m_strand(io_service), m_socket(io_service), m_queuePtr(queuePtr) {
            }

            void do_read()
            {
                auto handler = [self = shared_from_this()](const boost::system::error_code& err, size_t bytes_transferred) {
                    self->handle_read(err, bytes_transferred);
                };

                boost::asio::async_read_until(m_socket, m_buffer, '\0', m_strand.wrap(handler) );
            }

            void process(const boost::system::error_code &ec, std::size_t bytes_transferred)
            {
                std::string buff {
                        buffers_begin(m_buffer.data()),
                        buffers_begin(m_buffer.data()) + bytes_transferred
                        - 1};

                TRACE("Receive " << bytes_transferred << " from " << m_socket.remote_endpoint() << ": `" << buff << "` EC: " << ec.message());

                try
                {
                    if(!m_queuePtr->enqueue(std::make_unique<Record<Json>>(std::move(buff), m_socket.remote_endpoint())))
                    {
                        ERROR("Json TCP Input queue is full... Dropping...");
                    }
                }
                catch (JsonException &e)
                {
                    WARNING("Buffer decode error: " << e.what() << " from " << m_socket.remote_endpoint());
                }
            }

            void handle_read(const boost::system::error_code& err, size_t bytes_transferred)
            {
                if( bytes_transferred > 0 )
                {
                    process(err, bytes_transferred);
                    m_buffer.consume(bytes_transferred);
                }

                if (!err)
                {
                    do_read();
                }
                else if (err == boost::asio::error::eof)
                {
                    m_socket.close();
                }
                else
                {
                    WARNING("Error (recv): " << err.message());
                }
            }

            boost::asio::io_service::strand m_strand;
            boost::asio::streambuf m_buffer;
            tcp::socket m_socket;
            RecordQueuePtr<Json> m_queuePtr;
        };

    public:

        explicit Server(RecordQueuePtr<Json> queuePtr, boost::asio::io_service &io_service, short port)
                : m_acceptor(io_service, tcp::endpoint(tcp::v4(), port)),
                  m_queuePtr(queuePtr)
        {
            start_accept();
        }

        Server(const Server&) = delete;
        Server& operator=(const Server&) = delete;

    private:

        void start_accept()
        {
            tcp_connection::pointer new_connection =
                    tcp_connection::create(m_acceptor.get_io_service(), m_queuePtr);

            auto handler = [this, new_connection](const boost::system::error_code &error) {
                handle_accept(new_connection, error);
            };

            m_acceptor.async_accept(new_connection->socket(), handler);
        }

        void handle_accept(tcp_connection::pointer new_connection,
                           const boost::system::error_code &error)
        {
            DEBUG("Connection from: " << new_connection->socket().remote_endpoint().address());

            if (!error) {
                new_connection->start();
            } else
            {
                WARNING("Error (accept): " << error.message());
            }

            start_accept();
        }

        tcp::acceptor m_acceptor;
        RecordQueuePtr<Json> m_queuePtr;
    };
}


