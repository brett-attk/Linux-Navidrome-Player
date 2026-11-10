#include "ui/connect_dialog.hpp"

namespace radio::ui {

ConnectDialog::ConnectDialog(GtkWindow* parent, AppConfig initial)
    : parent_(parent), initial_(std::move(initial)) {}

void ConnectDialog::present(FinishedCb cb) {
  finished_ = std::move(cb);

  window_ = GTK_WINDOW(gtk_window_new());
  gtk_window_set_title(window_, "Connect to Navidrome");
  gtk_window_set_transient_for(window_, parent_);
  gtk_window_set_modal(window_, TRUE);
  gtk_window_set_default_size(window_, 480, 520);
  gtk_window_set_destroy_with_parent(window_, TRUE);

  auto* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(root, 16);
  gtk_widget_set_margin_bottom(root, 16);
  gtk_widget_set_margin_start(root, 16);
  gtk_widget_set_margin_end(root, 16);
  gtk_window_set_child(window_, root);

  auto* form = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_box_append(GTK_BOX(root), form);

  auto add_labeled_entry = [&](const char* label, bool password) {
    auto* l = gtk_label_new(label);
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(form), l);
    auto* e = GTK_ENTRY(gtk_entry_new());
    if (password) {
      gtk_entry_set_visibility(e, FALSE);
      gtk_entry_set_input_purpose(e, GTK_INPUT_PURPOSE_PASSWORD);
    }
    gtk_box_append(GTK_BOX(form), GTK_WIDGET(e));
    return e;
  };

  url_entry_ = add_labeled_entry("Server URL", false);
  user_entry_ = add_labeled_entry("Username", false);
  pass_entry_ = add_labeled_entry("Password", true);

  gtk_editable_set_text(GTK_EDITABLE(url_entry_), initial_.server_url.c_str());
  gtk_editable_set_text(GTK_EDITABLE(user_entry_), initial_.username.c_str());
  gtk_editable_set_text(GTK_EDITABLE(pass_entry_), initial_.password.c_str());

  auto* discover_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_append(GTK_BOX(root), discover_row);
  discover_button_ = GTK_BUTTON(gtk_button_new_with_label("Discover on LAN"));
  gtk_box_append(GTK_BOX(discover_row), GTK_WIDGET(discover_button_));
  status_label_ = GTK_LABEL(gtk_label_new(""));
  gtk_widget_set_halign(GTK_WIDGET(status_label_), GTK_ALIGN_START);
  gtk_label_set_wrap(status_label_, TRUE);
  gtk_box_append(GTK_BOX(discover_row), GTK_WIDGET(status_label_));

  auto* disc_label = gtk_label_new("Discovered servers");
  gtk_widget_set_halign(disc_label, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(root), disc_label);

  auto* scroll = gtk_scrolled_window_new();
  gtk_widget_set_vexpand(scroll, TRUE);
  gtk_box_append(GTK_BOX(root), scroll);
  discovered_list_ = GTK_LIST_BOX(gtk_list_box_new());
  gtk_list_box_set_selection_mode(discovered_list_, GTK_SELECTION_SINGLE);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll),
                                GTK_WIDGET(discovered_list_));

  auto* buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(buttons, GTK_ALIGN_END);
  gtk_box_append(GTK_BOX(root), buttons);
  auto* cancel = gtk_button_new_with_label("Cancel");
  auto* connect = gtk_button_new_with_label("Connect");
  gtk_widget_add_css_class(connect, "suggested-action");
  gtk_box_append(GTK_BOX(buttons), cancel);
  gtk_box_append(GTK_BOX(buttons), connect);

  g_signal_connect_swapped(discover_button_, "clicked",
                           G_CALLBACK(+[](gpointer self) {
                             static_cast<ConnectDialog*>(self)->on_discover();
                           }),
                           this);
  g_signal_connect_swapped(connect, "clicked",
                           G_CALLBACK(+[](gpointer self) {
                             static_cast<ConnectDialog*>(self)->on_connect();
                           }),
                           this);
  g_signal_connect_swapped(cancel, "clicked",
                           G_CALLBACK(+[](gpointer self) {
                             static_cast<ConnectDialog*>(self)->on_cancel();
                           }),
                           this);
  auto fill_url = +[](GtkListBox*, GtkListBoxRow* row, gpointer self) {
    auto* dlg = static_cast<ConnectDialog*>(self);
    if (!row) {
      return;
    }
    const int idx = gtk_list_box_row_get_index(row);
    if (idx < 0 || idx >= static_cast<int>(dlg->discovered_.size())) {
      return;
    }
    gtk_editable_set_text(
        GTK_EDITABLE(dlg->url_entry_),
        dlg->discovered_[static_cast<std::size_t>(idx)].url.c_str());
  };
  g_signal_connect(discovered_list_, "row-selected", G_CALLBACK(fill_url), this);
  g_signal_connect(discovered_list_, "row-activated", G_CALLBACK(fill_url), this);
  g_signal_connect(
      window_, "close-request",
      G_CALLBACK(+[](GtkWindow*, gpointer self) -> gboolean {
        static_cast<ConnectDialog*>(self)->on_cancel();
        return TRUE;
      }),
      this);

  gtk_window_present(window_);
}

void ConnectDialog::populate_discovered(
    const std::vector<discovery::ServerInfo>& servers) {
  discovered_ = servers;
  while (auto* child = gtk_widget_get_first_child(GTK_WIDGET(discovered_list_))) {
    gtk_list_box_remove(discovered_list_, child);
  }
  for (const auto& s : discovered_) {
    auto* row = gtk_list_box_row_new();
    auto* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_top(box, 6);
    gtk_widget_set_margin_bottom(box, 6);
    gtk_widget_set_margin_start(box, 8);
    gtk_widget_set_margin_end(box, 8);
    auto* name = gtk_label_new(s.name.c_str());
    gtk_widget_set_halign(name, GTK_ALIGN_START);
    gtk_widget_add_css_class(name, "heading");
    auto* url = gtk_label_new(s.url.c_str());
    gtk_widget_set_halign(url, GTK_ALIGN_START);
    gtk_widget_add_css_class(url, "dim-label");
    gtk_box_append(GTK_BOX(box), name);
    gtk_box_append(GTK_BOX(box), url);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    gtk_list_box_append(discovered_list_, row);
  }
}

void ConnectDialog::on_discover() {
  gtk_widget_set_sensitive(GTK_WIDGET(discover_button_), FALSE);
  gtk_label_set_text(status_label_, "Searching…");
  auto alive = alive_;
  discovery::LanDiscovery::start(
      [this, alive](const std::string& status) {
        if (!*alive || !status_label_) {
          return;
        }
        gtk_label_set_text(status_label_, status.c_str());
      },
      [this, alive](std::vector<discovery::ServerInfo> servers) {
        if (!*alive) {
          return;
        }
        populate_discovered(servers);
        gtk_widget_set_sensitive(GTK_WIDGET(discover_button_), TRUE);
      });
}

void ConnectDialog::on_connect() {
  if (finished_emitted_) {
    return;
  }
  finished_emitted_ = true;
  *alive_ = false;
  ConnectResult result;
  result.server_url = gtk_editable_get_text(GTK_EDITABLE(url_entry_));
  result.username = gtk_editable_get_text(GTK_EDITABLE(user_entry_));
  result.password = gtk_editable_get_text(GTK_EDITABLE(pass_entry_));
  auto cb = finished_;
  gtk_window_destroy(window_);
  window_ = nullptr;
  if (cb) {
    cb(result);
  }
}

void ConnectDialog::on_cancel() {
  if (finished_emitted_) {
    return;
  }
  finished_emitted_ = true;
  *alive_ = false;
  auto cb = finished_;
  if (window_) {
    gtk_window_destroy(window_);
    window_ = nullptr;
  }
  if (cb) {
    cb(std::nullopt);
  }
}

}  // namespace radio::ui
