# TGLES — Chính sách bảo mật

TGLES (TGL) là lớp chuyển đổi OpenGL ES 3.2 → Metal cho iOS và macOS. Nó được phân phối theo giấy phép MIT.

## Dữ liệu chúng tôi thu thập

**Không có.** TGLES là thư viện chuyển đổi đồ họa. Nó không thu thập, lưu trữ hoặc truyền bất kỳ dữ liệu cá nhân nào. Không có phân tích, không có télémétri, không có quảng cáo và không phân hạng người dùng.

## Hoạt động chỉ ở cục bộ

TGLES chạy hoàn toàn trên thiết bị chủ:

- Xác thực máy trạng thái OpenGL ES 3.2 cục bộ
- Chuyển đổi shader GLSL ES thành Metal Shading Language cục bộ
- Trình bày khung hình qua CAMetalLayer hoặc đọc đầu ra không đầu cuối

Không dữ liệu nào rời khỏi thiết bị trừ khi ứng dụng của bạn tự gửi nó.

## Nhật ký

Nhật ký chẩn đoán có thể được ghi để gỡ lỗi. Chúng chỉ ở cục bộ và không bao giờ được tải lên bởi thư viện.

## Trẻ em

TGLES không thu thập dữ liệu từ bất kỳ ai, bao gồm cả trẻ em.

## Quyền của bạn

Vì không có dữ liệu được thu thập, không có gì để truy cập, xuất hoặc xóa ở phía chúng tôi. Tất cả trạng thái kết xuất sống trong tiến trình ứng dụng của bạn.

## Liên hệ

Mở sự kiện trên [kho lưu trữ GitHub của TGLES](https://github.com/Witch-Launcher/TGLES).
