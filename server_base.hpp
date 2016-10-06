#ifndef SERVER_BASE_HPP
#define SERVER_BASE_HPP


#include <unordered_map>
#include <thread>
#include <regex>
#include <map>
#include <vector>
#include <boost/asio.hpp>

namespace FrostWeb
{
  struct Request
  {
    std::string method, path, http_version;
    std::shared_ptr<std::istream> content;
    // request header
    std::unordered_map<std::string, std::string> header;
    // regex for path match
    std::smatch path_match;
  };

  // resource member in ServerBase
  typedef std::map<std::string,
                   std::unordered_map<std::string,
                                      std::function<void(std::ostream&, Request&)>>> resource_type;

  template<typename socket_type>
  class ServerBase
  {
  public:
    resource_type resource;
    resource_type default_resource;

    ServerBase(unsigned short port, size_t num_threads=1) :
      endpoint(boost::asio::ip::tcp::v4(), port),
      acceptor(m_io_service, endpoint),
      num_threads(num_threads) { }

    // start server
    void start(unsigned short);

  protected:

    // io_service
    boost::asio::io_service m_io_service;
    // endpoint for construct a accepter
    boost::asio::ip::tcp::endpoint endpoint;
    boost::asio::ip::tcp::acceptor acceptor;

    std::vector<resource_type::iterator> all_resources;

    // thread pool
    size_t num_threads;
    std::vector<std::thread> threads;

    Request parse_request(std::istream&) const;

    void respond(std::shared_ptr<socket_type> socket, std::shared_ptr<Request> request) const;

    virtual void accept() { }
    // process request and respond
    void process_request_and_respond(std::shared_ptr<socket_type> socket) const;
  };

  template<typename socket_type>
  class Server : public ServerBase<socket_type> { };

  template<typename socket_type>
  void ServerBase<socket_type>::start(unsigned short port)
  {
    for(auto it=resource.begin(); it!=resource.end(); ++it){
      all_resources.push_back(it);
    }
    for(auto it=default_resource.begin(); it!=default_resource.end(); ++it){
      all_resources.push_back(it);
    }

    accept();

    // add threads
    std::cout << "Server start at port: " << port << std::endl;
    for(size_t c=1; c<num_threads; c++){
      std::cout << "run server in thread: " << c << std::endl;
      threads.emplace_back([this](){
          m_io_service.run();
        });
    }

    // main thread
    std::cout << "run server in thread: main" << std::endl;
    m_io_service.run();

    // wait for all threads to complete
    for(auto& t: threads)
      t.join();
  }

  template<typename socket_type>
  Request ServerBase<socket_type>::parse_request(std::istream& stream) const
  {
    std::cout << "----------start parse request----------" << std::endl;

    Request request;
    std::regex e("^([^ ]*) ([^ ]*) HTTP/([^ ]*)$");
    std::smatch sub_match;

    std::string line;
    getline(stream, line);
    line.pop_back();

    std::cout << "dell with url:" << std::endl;
    if(std::regex_match(line, sub_match, e)){
      int i = 0;
      for(auto &sub: sub_match)
        std::cout << "url" << i++ << ": " << sub << std::endl;
      std::cout << std::endl;
      request.method = sub_match[1];
      request.path = sub_match[2];
      request.http_version = sub_match[3];

      std::cout << "dell with header:" << std::endl;
      bool matched;
      e = "^([^:]*): ?(.*)$";
      do{
        getline(stream, line);
        line.pop_back();
        matched = std::regex_match(line, sub_match, e);
        if(matched){
          request.header[sub_match[1]] = sub_match[2];
          std::cout << sub_match[1] << " : " << sub_match[2] << std::endl;
        }
      } while(matched==true);
    }
    std::cout << "----------parse request ended---------" << std::endl << std::endl;
    return request;
  }

  template<typename socket_type>
  void ServerBase<socket_type>::
  process_request_and_respond(std::shared_ptr<socket_type> socket) const
  {
    // shared_ptr -> boost::asio::streambuf
    auto read_buffer = std::make_shared<boost::asio::streambuf>();

    boost::asio::async_read_until(*socket, *read_buffer, "\r\n\r\n",
                                  [this, socket, read_buffer](const boost::system::error_code& ec, size_t bytes_transferred){
                                    if(!ec){
                                      size_t total = read_buffer->size();

                                      std::istream stream(read_buffer.get());

                                      auto request = std::make_shared<Request>();
                                      *request = parse_request(stream);

                                      size_t num_addtional_bytes = total - bytes_transferred;

                                      if(request->header.count("Content-Length")>0){
                                        boost::asio::async_read(*socket, *read_buffer,
                                        boost::asio::transfer_exactly(stoull(request->header["Content-Length"]) - num_addtional_bytes),
                                                                [this, socket, read_buffer, request](const boost::system::error_code& ec, size_t bytes_transferred){
                                                                  if(!ec){
                                                                    request->content = std::shared_ptr<std::istream>(new std::istream(read_buffer.get()));
                                                                    respond(socket, request);
                                                                  }
                                                                });
                                      } else {
                                        respond(socket, request);
                                      }
                                    }
                                  });
  }

  template<typename socket_type>
  void ServerBase<socket_type>::respond(std::shared_ptr<socket_type> socket, std::shared_ptr<Request> request) const
  {
    std::cout << "-----------make respose----------" << std::endl;
    for(auto res_it: all_resources){
      std::cout << "match pattern: " << res_it->first << std::endl;
      std::regex e(res_it->first);
      std::smatch sm_res;
      std::cout << "match path:" << request->path << std::endl;
      if(std::regex_match(request->path, sm_res, e)){
        std::cout << "match method:" << request->method << std::endl;
        if(res_it->second.count(request->method)>0){
          request->path_match = move(sm_res);

          auto write_buffer = std::make_shared<boost::asio::streambuf>();
          std::ostream response(write_buffer.get());
          res_it->second[request->method](response, *request);

          boost::asio::async_write(*socket, *write_buffer,
                                   [this, socket, request, write_buffer](const boost::system::error_code& ec, size_t bytes_transferred){
                                     if(!ec && stof(request->http_version)>1.05)
                                       process_request_and_respond(socket);
                                   });
          std::cout << "----------make response end---------" << std::endl << std::endl;
          return;
        }
        std::cout << "# filed match method:" << request->method << std::endl;
      }
      std::cout << "# filed match path: " << request->path << std::endl;
    }
    std::cout << "# filed match pattern" << std::endl;
  }
}

#endif
