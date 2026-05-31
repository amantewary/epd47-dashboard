#pragma once

#include "types.h"
#include <vector>

// Fetch today's and overdue todos from Home Assistant into todoList.
// Returns true when the visible display data changed.
bool fetchTodos(std::vector<TodoItem> &todoList);
