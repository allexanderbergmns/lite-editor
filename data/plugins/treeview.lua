local core = require "core"
local common = require "core.common"
local command = require "core.command"
local config = require "core.config"
local keymap = require "core.keymap"
local style = require "core.style"
local View = require "core.view"

config.treeview_size = 200 * SCALE

local function get_depth(filename)
  local n = 0
  for sep in filename:gmatch("[\\/]") do
    n = n + 1
  end
  return n
end


-- prompts for a name and creates a new file or folder inside the directory
-- that the given tree view currently targets (see TreeView:get_target_dir)
local function create_item(view, kind)
  local dir = view:get_target_dir()
  local prompt = (kind == "dir") and "New Folder Name" or "New File Name"

  core.command_view:enter(prompt, function(text)
    local name = text:match("^%s*(.-)%s*$")
    if name == "" then
      return
    elseif name:find("[\\/]") then
      core.error("Name cannot contain a path separator")
      return
    end

    local path = (dir == ".") and name or (dir .. PATHSEP .. name)
    if system.get_file_info(path) then
      core.error("%q already exists", path)
      return
    end

    if kind == "dir" then
      local ok, err = system.mkdir(path)
      if not ok then
        core.error("Could not create folder %q: %s", path, err)
        return
      end
      core.log("Created folder %q", path)
      view:expand_to(dir)
      core.rescan_project()
    else
      local fp, err = io.open(path, "wb")
      if not fp then
        core.error("Could not create file %q: %s", path, err)
        return
      end
      fp:close()
      core.log("Created file %q", path)
      view:expand_to(dir)
      core.rescan_project()
      core.root_view:open_doc(core.open_doc(path))
    end
  end)
end


-- recursively deletes a file or directory from disk, without shelling out
-- (os.remove handles files and empty directories on every platform)
local function remove_recursive(path)
  local info = system.get_file_info(path)
  if not info then
    return false, "path does not exist"
  end
  if info.type == "dir" then
    for _, name in ipairs(system.list_dir(path) or {}) do
      local ok, err = remove_recursive(path .. PATHSEP .. name)
      if not ok then return false, err end
    end
  end
  if not os.remove(path) then
    return false, "could not remove " .. path
  end
  return true
end


-- asks for confirmation, then deletes the selected file or folder
local function delete_item(view)
  local item = view.selected_item or view.hovered_item
  if not item then return end

  local path = item.filename
  local message = (item.type == "dir")
    and string.format("Delete folder \"%s\" and all of its contents?", path)
    or string.format("Delete file \"%s\"?", path)

  if not system.show_confirm_dialog("Delete", message) then
    return
  end

  local ok, err = remove_recursive(path)
  if not ok then
    core.error("Could not delete %q: %s", path, err)
    return
  end

  core.log("Deleted %q", path)
  core.rescan_project()
end


local TreeView = View:extend()

function TreeView:new()
  TreeView.super.new(self)
  self.scrollable = true
  self.visible = true
  self.init_size = true
  self.cache = {}
end


function TreeView:get_cached(item)
  local t = self.cache[item.filename]
  if not t then
    t = {}
    t.filename = item.filename
    t.abs_filename = system.absolute_path(item.filename)
    t.name = t.filename:match("[^\\/]+$")
    t.depth = get_depth(t.filename)
    t.type = item.type
    self.cache[t.filename] = t
  end
  return t
end


function TreeView:get_name()
  return "Project"
end


function TreeView:get_item_height()
  return style.font:get_height() + style.padding.y
end


function TreeView:check_cache()
  -- invalidate cache's skip values if project_files has changed
  if core.project_files ~= self.last_project_files then
    for _, v in pairs(self.cache) do
      v.skip = nil
    end
    self.last_project_files = core.project_files
  end
end


function TreeView:each_item()
  return coroutine.wrap(function()
    self:check_cache()
    local ox, oy = self:get_content_offset()
    local y = oy + style.padding.y
    local w = self.size.x
    local h = self:get_item_height()

    local i = 1
    while i <= #core.project_files do
      local item = core.project_files[i]
      local cached = self:get_cached(item)

      coroutine.yield(cached, ox, y, w, h)
      y = y + h
      i = i + 1

      if not cached.expanded then
        if cached.skip then
          i = cached.skip
        else
          local depth = cached.depth
          while i <= #core.project_files do
            local filename = core.project_files[i].filename
            if get_depth(filename) <= depth then break end
            i = i + 1
          end
          cached.skip = i
        end
      end
    end
  end)
end


function TreeView:on_mouse_moved(px, py)
  self.hovered_item = nil
  if self.menu then
    local menu = self.menu
    local item_h = self:get_item_height()
    local first = menu.y + style.padding.y / 2
    menu.hovered = nil
    if px >= menu.x and px <= menu.x + menu.w
       and py >= menu.y and py <= menu.y + menu.h then
      local idx = math.floor((py - first) / item_h) + 1
      if idx >= 1 and idx <= #menu.items then
        menu.hovered = idx
      end
    end
    return
  end
  for item, x,y,w,h in self:each_item() do
    if px > x and py > y and px <= x + w and py <= y + h then
      self.hovered_item = item
      break
    end
  end
end


function TreeView:on_mouse_pressed(button, x, y)
  if self.menu then
    local menu = self.menu
    self.menu = nil
    if button == "left" and menu.hovered and menu.items[menu.hovered] then
      local kind = menu.items[menu.hovered].kind
      if kind == "delete" then
        delete_item(self)
      else
        create_item(self, kind)
      end
    end
    return
  end

  if button == "right" then
    if self.hovered_item then
      self.selected_item = self.hovered_item
    end
    self:open_context_menu(x, y)
    return
  end

  if not self.hovered_item then
    return
  elseif self.hovered_item.type == "dir" then
    self.selected_item = self.hovered_item
    self.hovered_item.expanded = not self.hovered_item.expanded
  else
    self.selected_item = self.hovered_item
    core.try(function()
      core.root_view:open_doc(core.open_doc(self.hovered_item.filename))
    end)
  end
end


-- right-click (button 3) context menu, drawn as an overlay inside the tree
function TreeView:open_context_menu(x, y)
  local items = {
    { text = "New File",   kind = "file" },
    { text = "New Folder", kind = "dir"  },
  }
  if self.selected_item or self.hovered_item then
    table.insert(items, { text = "Delete", kind = "delete" })
  end
  local w = 0
  for _, item in ipairs(items) do
    w = math.max(w, style.font:get_width(item.text))
  end
  w = w + style.padding.x * 2
  local h = #items * self:get_item_height() + style.padding.y

  -- keep the menu inside this view so it is not clipped by the view bounds
  local vx, vy = self.position.x, self.position.y
  if x + w > vx + self.size.x then x = vx + self.size.x - w end
  if y + h > vy + self.size.y then y = vy + self.size.y - h end
  if x < vx then x = vx end
  if y < vy then y = vy end

  self.menu = { x = x, y = y, w = w, h = h, hovered = nil, items = items }
end


function TreeView:draw_menu()
  local menu = self.menu
  local x, y = menu.x, menu.y
  local item_h = self:get_item_height()
  local div = style.divider_size

  renderer.draw_rect(x, y, menu.w, menu.h, style.background3)
  renderer.draw_rect(x, y, menu.w, div, style.line_highlight)
  renderer.draw_rect(x, y + menu.h - div, menu.w, div, style.line_highlight)
  renderer.draw_rect(x, y, div, menu.h, style.line_highlight)
  renderer.draw_rect(x + menu.w - div, y, div, menu.h, style.line_highlight)

  local iy = y + style.padding.y / 2
  for i, item in ipairs(menu.items) do
    local color = style.text
    if i == menu.hovered then
      renderer.draw_rect(x, iy, menu.w, item_h, style.line_highlight)
      color = style.accent
    end
    common.draw_text(style.font, color, item.text, nil,
      x + style.padding.x, iy, 0, item_h)
    iy = iy + item_h
  end
end


-- directory that new files/folders are created in: the selected item if it is a
-- directory, the containing directory if it is a file, otherwise the project root
function TreeView:get_target_dir()
  local item = self.selected_item or self.hovered_item
  if not item then
    return "."
  elseif item.type == "dir" then
    return item.filename
  end
  return item.filename:match("^(.*)[\\/][^\\/]+$") or "."
end


-- expand every cached ancestor of `filename` so a newly created item is visible
function TreeView:expand_to(filename)
  local path = filename
  while path and path ~= "" and path ~= "." do
    local cached = self.cache[path]
    if cached then
      cached.expanded = true
    end
    path = path:match("^(.*)[\\/][^\\/]+$")
  end
end


function TreeView:update()
  -- update width
  local dest = self.visible and config.treeview_size or 0
  if self.init_size then
    self.size.x = dest
    self.init_size = false
  else
    self:move_towards(self.size, "x", dest)
  end

  TreeView.super.update(self)
end


function TreeView:draw()
  self:draw_background(style.background2)

  local icon_width = style.icon_font:get_width("D")
  local spacing = style.font:get_width(" ") * 2

  local doc = core.active_view.doc
  local active_filename = doc and system.absolute_path(doc.filename or "")

  for item, x,y,w,h in self:each_item() do
    local color = style.text

    -- highlight active_view doc
    if item.abs_filename == active_filename then
      color = style.accent
    end

    -- hovered/selected item background
    if item == self.selected_item then
      renderer.draw_rect(x, y, w, h, style.line_highlight)
      color = style.accent
    elseif item == self.hovered_item then
      renderer.draw_rect(x, y, w, h, style.line_highlight)
      color = style.accent
    end

    -- icons
    x = x + item.depth * style.padding.x + style.padding.x
    if item.type == "dir" then
      local icon1 = item.expanded and "-" or "+"
      local icon2 = item.expanded and "D" or "d"
      common.draw_text(style.icon_font, color, icon1, nil, x, y, 0, h)
      x = x + style.padding.x
      common.draw_text(style.icon_font, color, icon2, nil, x, y, 0, h)
      x = x + icon_width
    else
      x = x + style.padding.x
      common.draw_text(style.icon_font, color, "f", nil, x, y, 0, h)
      x = x + icon_width
    end

    -- text
    x = x + spacing
    x = common.draw_text(style.font, color, item.name, nil, x, y, 0, h)
  end

  if self.menu then
    self:draw_menu()
  end
end


-- init
local view = TreeView()
local node = core.root_view:get_active_node()
node:split("left", view, true)

-- register commands and keymap
command.add(nil, {
  ["treeview:toggle"] = function()
    view.visible = not view.visible
  end,

  ["treeview:create-file"] = function()
    create_item(view, "file")
  end,

  ["treeview:create-folder"] = function()
    create_item(view, "dir")
  end,
})

keymap.add {
  ["ctrl+\\"] = "treeview:toggle",
  ["ctrl+shift+n"] = "treeview:create-file",
  ["ctrl+shift+m"] = "treeview:create-folder",
}
