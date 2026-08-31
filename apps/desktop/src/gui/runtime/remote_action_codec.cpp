#include "runtime/remote_action_codec.h"

#include <remote_action.h>

#include <cstdlib>
#include <cstring>

namespace crossdesk::remote_action_codec {

std::vector<char> Serialize(const RemoteAction &action) {
  std::vector<char> buffer;
  buffer.push_back(static_cast<char>(action.type));

  auto insert_bytes = [&](const void *ptr, size_t len) {
    buffer.insert(buffer.end(), static_cast<const char *>(ptr),
                  static_cast<const char *>(ptr) + len);
  };

  if (action.type == ControlType::host_infomation) {
    insert_bytes(&action.i.host_name_size, sizeof(size_t));
    insert_bytes(action.i.host_name, action.i.host_name_size);

    size_t num = action.i.display_num;
    insert_bytes(&num, sizeof(size_t));

    for (size_t i = 0; i < num; ++i) {
      const size_t len = std::strlen(action.i.display_list[i]);
      insert_bytes(&len, sizeof(size_t));
      insert_bytes(action.i.display_list[i], len);
    }

    insert_bytes(action.i.left, sizeof(int) * num);
    insert_bytes(action.i.top, sizeof(int) * num);
    insert_bytes(action.i.right, sizeof(int) * num);
    insert_bytes(action.i.bottom, sizeof(int) * num);
  }

  return buffer;
}

bool Deserialize(const char *data, size_t size, RemoteAction &out) {
  size_t offset = 0;
  auto read = [&](void *dst, size_t len) -> bool {
    if (offset + len > size) {
      return false;
    }
    std::memcpy(dst, data + offset, len);
    offset += len;
    return true;
  };

  if (size < 1) {
    return false;
  }
  out.type = static_cast<ControlType>(data[offset++]);

  if (out.type == ControlType::host_infomation) {
    size_t name_len;
    if (!read(&name_len, sizeof(size_t)) ||
        name_len >= sizeof(out.i.host_name)) {
      return false;
    }
    if (!read(out.i.host_name, name_len)) {
      return false;
    }
    out.i.host_name[name_len] = '\0';
    out.i.host_name_size = name_len;

    size_t num;
    if (!read(&num, sizeof(size_t))) {
      return false;
    }
    out.i.display_num = num;

    out.i.display_list =
        static_cast<char **>(std::malloc(num * sizeof(char *)));
    for (size_t i = 0; i < num; ++i) {
      size_t len;
      if (!read(&len, sizeof(size_t)) || offset + len > size) {
        return false;
      }
      out.i.display_list[i] = static_cast<char *>(std::malloc(len + 1));
      std::memcpy(out.i.display_list[i], data + offset, len);
      out.i.display_list[i][len] = '\0';
      offset += len;
    }

    auto alloc_int_array = [&](int *&array) {
      array = static_cast<int *>(std::malloc(num * sizeof(int)));
      return read(array, num * sizeof(int));
    };

    return alloc_int_array(out.i.left) && alloc_int_array(out.i.top) &&
           alloc_int_array(out.i.right) && alloc_int_array(out.i.bottom);
  }

  return true;
}

void Free(RemoteAction &action) {
  if (action.type != ControlType::host_infomation) {
    return;
  }

  for (size_t i = 0; i < action.i.display_num; ++i) {
    std::free(action.i.display_list[i]);
  }
  std::free(action.i.display_list);
  std::free(action.i.left);
  std::free(action.i.top);
  std::free(action.i.right);
  std::free(action.i.bottom);

  action.i.display_list = nullptr;
  action.i.left = action.i.top = action.i.right = action.i.bottom = nullptr;
  action.i.display_num = 0;
}

} // namespace crossdesk::remote_action_codec
