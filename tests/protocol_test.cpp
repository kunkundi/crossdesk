#include <cstring>
#include <iostream>
#include <string>

#include "device_controller.h"
#include "cursor_position.h"
#include "file_transfer_protocol.h"
#include "stream_names.h"

namespace {

bool Expect(bool condition, const char* message) {
  if (condition) return true;
  std::cerr << message << '\n';
  return false;
}

}  // namespace

int main() {
  bool ok = true;

  crossdesk::RemoteAction unset_action{};
  ok &= Expect(unset_action.type == crossdesk::ControlType::invalid,
               "default RemoteAction should be invalid");
  ok &= Expect(unset_action.to_json().empty(),
               "invalid RemoteAction should not be serialized");

  crossdesk::RemoteAction parsed_invalid{};
  ok &= Expect(!parsed_invalid.from_json("{\"type\":-1}"),
               "invalid RemoteAction JSON should be rejected");

  crossdesk::RemoteAction unknown_action{};
  unknown_action.type = static_cast<crossdesk::ControlType>(999);
  ok &= Expect(unknown_action.to_json().empty(),
               "unknown RemoteAction type should not be serialized");

  crossdesk::RemoteAction mouse{};
  mouse.type = crossdesk::ControlType::mouse;
  mouse.m = {0.25f, 0.75f, -1, crossdesk::MouseFlag::wheel_vertical};
  crossdesk::RemoteAction parsed_mouse{};
  ok &= Expect(parsed_mouse.from_json(mouse.to_json()),
               "mouse JSON should decode");
  ok &= Expect(parsed_mouse.type == crossdesk::ControlType::mouse &&
                   parsed_mouse.m.x == mouse.m.x &&
                   parsed_mouse.m.y == mouse.m.y &&
                   parsed_mouse.m.s == mouse.m.s &&
                   parsed_mouse.m.flag == mouse.m.flag,
               "mouse JSON round trip changed fields");

  crossdesk::RemoteAction cursor{};
  cursor.type = crossdesk::ControlType::cursor_state;
  cursor.cs = {42, true, crossdesk::RemoteCursorShape::pointer,
               true, 0.25f, 0.75f, 0.001f, -0.002f, 1, true};
  crossdesk::RemoteAction parsed_cursor{};
  ok &= Expect(parsed_cursor.from_json(cursor.to_json()) &&
                   parsed_cursor.type == crossdesk::ControlType::cursor_state &&
                   parsed_cursor.cs.seq == cursor.cs.seq &&
                   parsed_cursor.cs.visible == cursor.cs.visible &&
                   parsed_cursor.cs.shape == cursor.cs.shape &&
                   parsed_cursor.cs.position_valid &&
                   parsed_cursor.cs.x == cursor.cs.x &&
                   parsed_cursor.cs.y == cursor.cs.y &&
                   parsed_cursor.cs.visual_offset_x ==
                       cursor.cs.visual_offset_x &&
                   parsed_cursor.cs.visual_offset_y ==
                       cursor.cs.visual_offset_y &&
                   parsed_cursor.cs.display_id == cursor.cs.display_id &&
                   parsed_cursor.cs.position_update,
               "cursor state JSON round trip changed position fields");

  crossdesk::RemoteAction legacy_cursor{};
  ok &= Expect(legacy_cursor.from_json(
                   "{\"type\":8,\"cursor_state\":{\"seq\":1,"
                   "\"visible\":true,\"shape\":0}}") &&
                   !legacy_cursor.cs.position_valid &&
                   legacy_cursor.cs.position_update &&
                   legacy_cursor.cs.display_id == -1,
               "legacy cursor state JSON should remain compatible");

  crossdesk::RemoteAction shape_only_cursor{};
  shape_only_cursor.type = crossdesk::ControlType::cursor_state;
  shape_only_cursor.cs = cursor.cs;
  shape_only_cursor.cs.position_update = false;
  crossdesk::RemoteAction parsed_shape_only_cursor{};
  ok &= Expect(parsed_shape_only_cursor.from_json(
                   shape_only_cursor.to_json()) &&
                   !parsed_shape_only_cursor.cs.position_update &&
                   parsed_shape_only_cursor.cs.shape == cursor.cs.shape,
               "shape-only cursor state should preserve update semantics");

  crossdesk::RemoteAction command{};
  command.type = crossdesk::ControlType::service_command;
  command.c.flag = crossdesk::ServiceCommandFlag::send_sas;
  crossdesk::RemoteAction parsed_command{};
  ok &= Expect(parsed_command.from_json(command.to_json()) &&
                   parsed_command.type ==
                       crossdesk::ControlType::service_command &&
                   parsed_command.c.flag ==
                       crossdesk::ServiceCommandFlag::send_sas,
               "service command JSON round trip failed");

  char display_name[] = "Built-in Display";
  char* display_names[] = {display_name};
  int left[] = {0};
  int top[] = {0};
  int right[] = {2560};
  int bottom[] = {1600};
  crossdesk::RemoteAction host{};
  host.type = crossdesk::ControlType::host_infomation;
  std::strcpy(host.i.host_name, "Test Mac");
  host.i.host_name_size = std::strlen(host.i.host_name);
  host.i.display_list = display_names;
  host.i.display_num = 1;
  host.i.left = left;
  host.i.top = top;
  host.i.right = right;
  host.i.bottom = bottom;
  crossdesk::RemoteAction parsed_host{};
  ok &= Expect(parsed_host.from_json(host.to_json()) &&
                   parsed_host.i.display_num == 1 &&
                   std::string(parsed_host.i.host_name) == "Test Mac" &&
                   std::string(parsed_host.i.display_list[0]) ==
                       "Built-in Display" &&
                   parsed_host.i.right[0] == 2560 &&
                   parsed_host.i.bottom[0] == 1600,
               "host information JSON round trip failed");
  crossdesk::FreeRemoteAction(parsed_host);

  const std::vector<crossdesk::DisplayInfo> displays = {
      crossdesk::DisplayInfo("Test Display", 0, 0, 1920, 1080)};
  crossdesk::CursorState normalized_cursor{};
  ok &= Expect(crossdesk::NormalizeCursorPosition(
                   960.0, 540.0, displays, 0, &normalized_cursor) &&
                   normalized_cursor.position_valid &&
                   normalized_cursor.x == 0.5f &&
                   normalized_cursor.y == 0.5f,
               "cursor position should use continuous display extents");

  const std::string name = "example.txt";
  const std::string payload = "CrossDesk protocol";
  const auto encoded = crossdesk::protocol::EncodeFileChunk(
      17, 0, payload.size(), payload.data(),
      static_cast<uint32_t>(payload.size()), &name, true, true);
  crossdesk::protocol::FileChunkView chunk;
  ok &= Expect(crossdesk::protocol::DecodeFileChunk(
                   encoded.data(), encoded.size(), &chunk),
               "file chunk should decode");
  ok &= Expect(chunk.header.file_id == 17 && chunk.file_name == name &&
                   chunk.payload_size == payload.size() &&
                   std::memcmp(chunk.payload, payload.data(), payload.size()) ==
                       0,
               "file chunk round trip changed fields");
  ok &= Expect(!crossdesk::protocol::DecodeFileChunk(
                   encoded.data(), encoded.size() - 1, &chunk),
               "truncated file chunk should be rejected");

  crossdesk::FileTransferAck ack{};
  ack.magic = crossdesk::kFileAckMagic;
  ack.file_id = 17;
  ack.acked_offset = payload.size();
  ack.total_size = payload.size();
  ack.flags = 1;
  const auto encoded_ack = crossdesk::protocol::EncodeFileTransferAck(ack);
  crossdesk::FileTransferAck decoded_ack{};
  ok &= Expect(crossdesk::protocol::DecodeFileTransferAck(
                   encoded_ack.data(), encoded_ack.size(), &decoded_ack) &&
                   decoded_ack.file_id == ack.file_id &&
                   decoded_ack.acked_offset == ack.acked_offset &&
                   decoded_ack.flags == ack.flags,
               "file acknowledgement round trip failed");

  ok &= Expect(std::string(crossdesk::protocol::kControlStream) ==
                       "control_data" &&
                   std::string(crossdesk::protocol::kFileFeedbackStream) ==
                       "file_feedback",
               "stream names changed");
  return ok ? 0 : 1;
}
