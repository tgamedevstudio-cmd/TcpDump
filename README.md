CÔNG CỤ TẤN CÔNG VỚI XOAY VÒNG PROXY
=======================================

THÔNG TIN
---------
Tên: Attack Tool with Proxy Rotation
Phiên bản: 2.0
Ngôn ngữ: C++ 17
Nền tảng: Windows x64
Yêu cầu: Administrator (cho SYN flood)

TÍNH NĂNG
---------
1. HTTP flood với xoay vòng proxy
2. Slowloris qua proxy
3. UDP flood
4. SYN flood (cần quyền admin)
5. Tự động tải proxy từ 5 nguồn
6. Kiểm tra proxy hoạt động
7. Xoay vòng proxy tự động
8. User-Agent ngẫu nhiên
9. Request đa dạng (path, query random)

CÀI ĐẶT
-------
1. Mở Visual Studio 2022
2. Tạo Console App (C++)
3. Copy toàn bộ mã nguồn
4. Project -> Properties -> Linker -> Input -> Additional Dependencies
5. Thêm: ws2_32.lib
6. Build -> Build Solution

SỬ DỤNG
-------
Cú pháp:
  tool.exe -t <đích> -m <phương thức> [tùy chọn]

Tùy chọn:
  -t <đích>       IP hoặc tên miền
  -m <phương thức> http, slowloris, udp, syn
  -p <cổng>       Cổng đích (mặc định: 80)
  -d <giây>       Thời gian chạy (mặc định: 10)
  -c <luồng>      Số luồng (mặc định: 1, tối đa: 100)
  --proxy         Bật xoay vòng proxy

VÍ DỤ
-----
1. HTTP flood với proxy:
   tool.exe -t example.com -m http -d 60 -c 10 --proxy

2. Slowloris không proxy:
   tool.exe -t example.com -m slowloris -p 80 -d 120

3. UDP flood:
   tool.exe -t 192.168.1.1 -m udp -p 53 -d 30 -c 4

4. SYN flood (cần admin):
   tool.exe -t 10.0.0.1 -m syn -p 80 -d 30 -c 100

5. HTTP flood không proxy:
   tool.exe -t example.com -m http -d 30 -c 5

NGUỒN PROXY
-----------
1. TheSpeedX PROXY-List (GitHub)
2. ShiftyTR Proxy-List (GitHub)
3. monosans proxy-list (GitHub)
4. proxyscrape.com API
5. proxy-list.download API

QUY TRÌNH HOẠT ĐỘNG
-------------------
1. Tải danh sách proxy từ 5 nguồn
2. Parse định dạng host:port
3. Kiểm tra proxy hoạt động (httpbin.org)
4. Lọc proxy theo thời gian phản hồi
5. Xoay vòng proxy mỗi request
6. Random User-Agent mỗi request
7. Random path và query parameter

CHI TIẾT KỸ THUẬT
-----------------
HTTP Flood:
  - Method: GET
  - Path: / + random(5-25 ký tự)
  - Query: random(8)=random(8)
  - Headers: Host, User-Agent, Accept, Referer, Connection
  - Random delay: 0-100ms

Slowloris:
  - Giữ kết nối mở
  - Gửi header mỗi 5 giây
  - Tối đa threads kết nối

UDP Flood:
  - Kích thước gói: 64-1400 bytes
  - Nội dung ngẫu nhiên
  - Delay: 100 microseconds

SYN Flood:
  - IP nguồn giả mạo 192.168.x.x
  - Cờ SYN
  - Cổng nguồn ngẫu nhiên

KẾT QUẢ
-------
[*] - Thông tin
[+] - Thành công
[-] - Lỗi
[!] - Cảnh báo

Thống kê hiển thị:
  [10s] Req:10000 | MB:15 | Mbps:12.5 | Rate:1000/s | Conn:500

LOG
---
File: attack_log.txt
Lưu toàn bộ lịch sử với timestamp

XỬ LÝ SỰ CỐ
-----------
1. Không có proxy hoạt động:
   - Hiển thị cảnh báo
   - Tiếp tục chạy không proxy

2. SYN flood lỗi:
   - Cần chạy Administrator
   - Kiểm tra tường lửa

3. Không phân giải được host:
   - Kiểm tra tên miền
   - Dùng IP trực tiếp

4. Kết nối chậm:
   - Giảm số luồng (-c)
   - Kiểm tra mạng

GIỚI HẠN
--------
- Proxy chỉ hỗ trợ HTTP (không SOCKS)
- HTTPS chưa hỗ trợ proxy connect
- SYN flood cần quyền Administrator
- Tối đa 100 luồng
=======================================
BẢN QUYỀN © 2025 - CHỈ DÙNG KIỂM THỬ
=======================================
