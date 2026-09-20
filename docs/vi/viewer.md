# Cửa sổ test tương tác (macOS Intel)

App Cocoa riêng (`apps/tgl_viewer.mm`, target `tgl_viewer`): vẽ phố khối
đứng bằng API OpenGL ES 3.2, đi qua `GlesContext::RenderFrame`, dịch sang
MSL rồi present lên `CAMetalLayer` thật của cửa sổ.

```sh
cmake --build build --target tgl_viewer
./build/tgl_viewer
```

## Nó chứng minh điều gì

Mọi pixel đều đi đường TGL thật, không vẽ Metal tay:

- GLES managers sở hữu scene: 2 `ARRAY_BUFFER` (xyz + rgba), VAO attribs
  0/1, program GLSL ES 3.20 với uniform `u_modelViewProj`, draw-FBO
  `COLOR_ATTACHMENT0` quyết định kích thước target.
- Ánh sáng diffuse hướng được bake trên CPU vào color stream
  (`TriNormal`/`ShadeFace` trong `apps/cube_render.h`).
- Depth thật trên GPU: renderbuffer `DEPTH_COMPONENT24` + `DEPTH_TEST`
  (LESS, write on, clear 1) → bridge tạo depth target Depth32Float,
  depth-stencil state và PSO riêng. Che khuất đúng từng pixel (trước đây
  painter sort bị tấm đất che khối sai).
- Present blit target offscreen ra drawable của cửa sổ.

## Scene

7 khối dọc cao 1.6–5.0 xoay vòng 3 màu RGB thuần (Đỏ/Xanh lá/Xanh dương),
1 khối xoay RGB ở giữa, mặt đất xám. Sort painter CPU vẫn giữ như thứ tự
ổn định bên dưới depth test.

## Panel HUD (song ngữ VI/EN)

- Mặc định theo locale hệ thống; đổi bằng segmented control đầu panel
  hoặc phím `L`. Toàn bộ UI là chữ, không icon/ảnh.
- FPS (EMA), frame ms (EMA) + worst, biểu đồ cột 180 frame
  (xanh <17.5ms, vàng <33.3ms, đỏ = drop) kèm vạch 16.7/33.3ms.
- Số verts/tris, draws, bridge serials (frame/done/swap), trạng thái cam,
  góc xoay, hướng đèn, kích thước FBO, version report, trạng thái lỗi.
- Panel help: đường đi khung hình + shader GLSL gốc + MSL của bridge.

## Layout co giãn

View Metal chiếm toàn bộ diện tích trống, panel HUD giữ 360pt bên phải.
Phóng to cửa sổ: hai vùng stats và help (scroll) chia nhau phần cao thêm
theo tỉ lệ; thu nhỏ: scroll + chiều cao tối thiểu + min-size cửa sổ
900×560 nên chữ không bao giờ đè nhau. FBO/depth target bám theo
backing-pixel của view mỗi frame.

## Điều khiển cam

- Kéo trái: xoay (yaw/pitch) • Kéo phải / Shift+kéo: di chuyển mục tiêu
- Cuộn chuột: zoom (3.5–34) • Space: bật/tắt tự xoay
- `R`: đặt lại cam • `L`: đổi VI/EN

## Ràng buộc NDC trên Metal (quan trọng)

Metal chỉ nhận clip-space z trong **[0, 1]** (GL là [-1, 1]); geometry
ngoài dải bị clip mất trước khi depth test chạy (đã probe trên Intel,
xem `RealDepthResolvesOverlap`). Ma trận perspective của viewer cho z
dương ở khoảng cách dùng bình thường; zoom cực sát (vật cách cam <~0.2)
có thể bị clip — giới hạn đã biết, chưa remap.

## Sự cố thường gặp

- Góc cam lạ (VD chui xuống dưới đất): nhấn `R` để reset.
- Cửa sổ thu quá nhỏ: render tạm dừng dưới 8px, phóng to lại là chạy.
- Máy không GPU (VM/remote): alert báo không khởi tạo được Metal.
- Chữ đè nhau: không xảy ra với layout hiện tại; nếu thấy, báo kèm ảnh.
