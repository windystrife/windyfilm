WindyFilm cho Canon 5D Mark II (firmware 2.1.2) - thẻ thử đầu tiên, 14/11/2026
==============================================================================

Module Magic Lantern tạo màu phim cho ảnh JPEG ngay lúc chụp.
Tác giả: WindyStrife. Giấy phép GPL-2.0 (LICENSE_WINDYFILM.txt).
Dữ liệu và thông số bên thứ ba: ATTRIBUTIONS.txt.

Đây là bản CHƯA từng chạy trên máy thật. Lần chạy đầu để kiểm tra; mọi lỗi đều
được ghi vào ML/LOGS/WINDYFLM.LOG. Đã gồm các sửa từ lần chạy thật đầu tiên trên
EOS M (14/11): log ghi nối tiếp và không mất dòng khi thẻ bận, module chỉ tạo /
xoá / đổi tên file khi máy đã ghi xong ảnh (tránh hai file cùng tên).


1. CÀI LÊN THẺ
--------------
- Dùng một thẻ CF đã cài Magic Lantern cho 5D2 (thẻ boot được). Xoá hết nội
  dung cũ rồi chép TẤT CẢ trong thư mục này vào gốc thẻ. Thẻ mới tinh thì chép
  xong chạy ML-SETUP.FIR (Menu > Firmware update) để bật boot.
- autoexec.bin và ML/modules/5D2_212.sym luôn đi đúng cặp. Đừng thay một trong
  hai bằng file từ thẻ khác.
- Thẻ này không có filmroll.mo. Đừng chép filmroll.mo vào: hai module tranh
  nhau cùng một điểm hook, và WindyFilm sẽ báo "Disable filmroll.mo".
- ML/SETTINGS/WINDYFLM.EN là file bật module lúc khởi động.

Cài đặt máy: JPEG L (kích thước M / S không được xử lý). Chỉ RAW thì không có
JPEG nên không có màu phim (RAW+L thì có).
Grain và Halation mặc định tắt trên 5D2 (mỗi nửa ảnh tốn vài giây).


2. DÙNG
-------
- Menu ML: Shoot > WindyFilm. Mở trình chọn phim bằng "Open film browser",
  hoặc bấm TRASH (không mở trong chế độ xem ảnh, nơi TRASH là xoá).
- Trong trình chọn phim:
    Đổi phim ............. joystick trái / phải
    Chọn hàng ............ joystick lên / xuống
    Đổi giá trị hàng ..... bánh xe trên hoặc bánh xe sau
    Nạp phim ............. SET hoặc nhấn joystick
    Nhóm kế tiếp ......... Picture Style
    Trang chi tiết ....... INFO
    Đóng ................. MENU hoặc TRASH
  Bấm nửa nút chụp: giao diện đóng, chụp như thường.
- Hàng Light: Auto (giữ WB của bạn), Day (WB theo phim), Cloudy, Tungsten.
  WB của bạn được trả lại khi về Auto hoặc khi tắt module.
- Hàng Print: Scan / Cine print / Paper (chỉ với phim có mô hình in).
- Output khác: Log (5 đường cong), Log + LUT (cube trong ML/data, xem
  README_FILMLUT.txt), Off.
- Input curve (cho Log + LUT và lon Film LUT): sRGB, Canon Film 1 hoặc Canon
  Neutral. Hai đường cong đo trên EOS M (Film 1: file trong User Def. 1,
  Contrast -2; Neutral: Contrast 0); trên 5D2 chưa kiểm, cứ để sRGB nếu không
  chắc.
- Xem trước trên LiveView: cần bật Overlay > Global Draw của ML.
- RAW probe chỉ có trên EOS M.


3. LẦN CHẠY ĐẦU: CẦN XEM
------------------------
1. ML/LOGS/WINDYFLM.LOG sau khi bật máy phải có dòng hook TOP / BOT với err 0,
   "hooks: installed (0)" và "pack ok: 49 tables".
2. Chụp một ảnh mỗi chế độ: phim (Print Scan / Cine / Paper), Log, Log + LUT,
   một lon Film LUT. Mỗi nửa ảnh có dòng "yuv TOP ..." / "yuv BOT ..." trong log.
3. Light Day / Cloudy / Tungsten rồi về Auto: WB trở lại như cũ.
4. Các phím của trình chọn phim; màu giao diện khi LiveView ở phía sau.
5. EXIF (UserComment) của ảnh có dòng "WindyFilm: ...".


4. FILE TRÊN THẺ
----------------
ML/modules/windyflm.mo    module
ML/data/WFFILMS.DAT       bảng màu các phim (Scan / Cine / Paper)
ML/data/WFCANS.DAT        hình lon phim
ML/data/WFFONTS.DAT       font chữ của giao diện (Ubuntu Sans, UBUNTU_FONT_LICENCE.txt)
ML/data/*.CUB             cube Film LUT (README_FILMLUT.txt)
ML/SETTINGS/WINDYFLM.CFG  cấu hình (module tự ghi)
ML/LOGS/WINDYFLM.LOG      nhật ký, ghi nối tiếp qua các lần bật máy; mỗi lần
                          bật máy bắt đầu bằng dòng "WindyFilm <ngày>: camera ..."
ML/LOGS/WINDYFLM.OLD      nhật ký cũ (khi WINDYFLM.LOG quá 512 KB lúc bật máy)
MD5SUMS.txt               md5 của mọi file khi làm thẻ
