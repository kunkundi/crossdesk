#include "ws_core.h"

#include <cstdlib>
#include <iostream>
#include <sstream>

#include "log.h"

WsCore::WsCore() {
  m_endpoint_.clear_access_channels(websocketpp::log::alevel::all);
  m_endpoint_.clear_error_channels(websocketpp::log::elevel::all);

  m_endpoint_.init_asio();
  m_endpoint_.start_perpetual();

  m_thread_ = std::thread(&client::run, &m_endpoint_);
}

WsCore::~WsCore() {
  destructed_ = true;
  running_ = false;

  cond_var_.notify_one();
  if (ping_thread_.joinable()) {
    ping_thread_.join();
  }

  m_endpoint_.stop_perpetual();

  if (m_thread_.joinable()) {
    m_thread_.join();
  }
}

int WsCore::Connect(std::string const &uri) {
  uri_ = uri;

  websocketpp::lib::error_code ec;

  client::connection_ptr con = m_endpoint_.get_connection(uri, ec);

  connection_handle_ = con->get_handle();

  if (ec) {
    LOG_ERROR("Connect initialization error: {}", ec.message());
    return -1;
  }

  con->set_open_handler(websocketpp::lib::bind(
      &WsCore::OnOpen, this, &m_endpoint_, websocketpp::lib::placeholders::_1));
  con->set_fail_handler(websocketpp::lib::bind(
      &WsCore::OnFail, this, &m_endpoint_, websocketpp::lib::placeholders::_1));
  con->set_close_handler(
      websocketpp::lib::bind(&WsCore::OnClose, this, &m_endpoint_,
                             websocketpp::lib::placeholders::_1));

  con->set_ping_handler(websocketpp::lib::bind(
      &WsCore::OnPing, this, websocketpp::lib::placeholders::_1,
      websocketpp::lib::placeholders::_2));

  con->set_pong_handler(websocketpp::lib::bind(
      &WsCore::OnPong, this, websocketpp::lib::placeholders::_1,
      websocketpp::lib::placeholders::_2));

  con->set_pong_timeout(1000);

  con->set_pong_timeout_handler(websocketpp::lib::bind(
      &WsCore::OnPongTimeout, this, websocketpp::lib::placeholders::_1,
      websocketpp::lib::placeholders::_2));

  con->set_message_handler(websocketpp::lib::bind(
      &WsCore::OnMessage, this, websocketpp::lib::placeholders::_1,
      websocketpp::lib::placeholders::_2));

  m_endpoint_.connect(con);

  ws_status_ = WsStatus::WsOpening;
  OnWsStatus(WsStatus::WsOpening);

  return 0;
}

void WsCore::Close(websocketpp::close::status::value code, std::string reason) {
  websocketpp::lib::error_code ec;

  m_endpoint_.close(connection_handle_, code, reason, ec);
  if (ec) {
    LOG_ERROR("Initiating close error: {}", ec.message());
  }
  OnWsStatus(WsStatus::WsClosed);
}

void WsCore::Send(std::string message) {
  websocketpp::lib::error_code ec;

  m_endpoint_.send(connection_handle_, message,
                   websocketpp::frame::opcode::text, ec);
  if (ec) {
    LOG_ERROR("Sending message error: {}", ec.message());
    return;
  }
}

void WsCore::Ping(websocketpp::connection_hdl hdl) {
  while (running_) {
    {
      std::unique_lock<std::mutex> lock(mtx_);
      cond_var_.wait_for(lock, std::chrono::seconds(interval_),
                         [this] { return !running_; });
    }

    auto con = m_endpoint_.get_con_from_hdl(hdl);
    if (con && con->get_state() == websocketpp::session::state::open) {
      websocketpp::lib::error_code ec;
      m_endpoint_.ping(hdl, "", ec);
      if (ec) {
        LOG_ERROR("Ping error: {}", ec.message());
        break;
      }
    }

    if (!running_) break;
  }
}

WsStatus WsCore::GetStatus() { return ws_status_; }

void WsCore::OnOpen(client *c, websocketpp::connection_hdl hdl) {
  ws_status_ = WsStatus::WsOpened;
  OnWsStatus(WsStatus::WsOpened);

  ping_thread_ = std::thread(&WsCore::Ping, this, hdl);
}

void WsCore::OnFail(client *c, websocketpp::connection_hdl hdl) {
  ws_status_ = WsStatus::WsFailed;
  OnWsStatus(WsStatus::WsFailed);

  Connect(uri_);
}

void WsCore::OnClose(client *c, websocketpp::connection_hdl hdl) {
  ws_status_ = WsStatus::WsClosed;
  if (running_) {
    OnWsStatus(WsStatus::WsClosed);
  }
}

bool WsCore::OnPing(websocketpp::connection_hdl hdl, std::string msg) {
  return true;
}

bool WsCore::OnPong(websocketpp::connection_hdl hdl, std::string msg) {
  return true;
}

void WsCore::OnPongTimeout(websocketpp::connection_hdl hdl, std::string msg) {
  if (timeout_count_ < 2) {
    timeout_count_++;
    return;
  }

  LOG_WARN("Pong timeout, reset connection");
  // m_endpoint_.close(hdl, websocketpp::close::status::normal,
  // "OnPongTimeout");
  ws_status_ = WsStatus::WsReconnecting;
  OnWsStatus(WsStatus::WsReconnecting);
  m_endpoint_.reset();
}

void WsCore::OnMessage(websocketpp::connection_hdl, client::message_ptr msg) {
  OnReceiveMessage(msg->get_payload());
}