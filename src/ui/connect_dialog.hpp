#pragma once

#include "config.hpp"
#include "discovery/lan_discovery.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtk/gtk.h>

namespace radio::ui {

struct ConnectResult {
  std::string server_url;
  std::string username;
  std::string password;
};

class ConnectDialog {
 public:
  using FinishedCb = std::function<void(std::optional<ConnectResult>)>;

  explicit ConnectDialog(GtkWindow* parent, AppConfig initial);
  void present(FinishedCb cb);

 private:
  void on_discover();
  void on_connect();
  void on_cancel();
  void populate_discovered(const std::vector<discovery::ServerInfo>& servers);

  GtkWindow* parent_ = nullptr;
  AppConfig initial_;
  FinishedCb finished_;

  GtkWindow* window_ = nullptr;
  GtkEntry* url_entry_ = nullptr;
  GtkEntry* user_entry_ = nullptr;
  GtkEntry* pass_entry_ = nullptr;
  GtkListBox* discovered_list_ = nullptr;
  GtkLabel* status_label_ = nullptr;
  GtkButton* discover_button_ = nullptr;
  std::vector<discovery::ServerInfo> discovered_;
  bool finished_emitted_ = false;
  // LAN discovery callbacks may arrive after the dialog is closed. Capturing
  // this shared flag lets those callbacks bail out safely (see on_discover).
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}  // namespace radio::ui
