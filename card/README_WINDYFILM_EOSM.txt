WindyFilm cho Canon EOS M (firmware 2.0.2) - thẻ thử đầu tiên, 14/11/2026
=========================================================================

Module Magic Lantern tạo màu phim cho ảnh JPEG ngay lúc chụp.
Tác giả: WindyStrife. Giấy phép GPL-2.0 (LICENSE_WINDYFILM.txt).
Dữ liệu và thông số bên thứ ba: ATTRIBUTIONS.txt.

Bản này sửa các lỗi thấy ở lần chạy thật đầu tiên (14/11, RAW probe): giờ
trong log hiện "u.u", dòng log bị mất khi thẻ bận, WFPROBE.BIN thiếu một bản
ghi, và hai file cùng tên WINDYFLM.LOG trong ML/LOGS. Module giờ chỉ tạo /
xoá / đổi tên file khi máy đã ghi xong ảnh, và chỉ tạo khi chắc chắn tên đó
không còn trên thẻ. Mọi lỗi đều được ghi vào ML/LOGS/WINDYFLM.LOG.

Thẻ cũ còn WINDYFLM2.LOG (bản trùng tên đã đổi tên) thì xoá được; module không
dùng tên đó.


1. CÀI LÊN THẺ
--------------
- Dùng một thẻ đã cài Magic Lantern cho EOS M (thẻ boot được). Xoá hết nội dung
  cũ rồi chép TẤT CẢ trong thư mục này vào gốc thẻ. Thẻ mới tinh thì chép xong
  chạy ML-SETUP.FIR (Menu > Firmware update) để bật boot.
- autoexec.bin và ML/modules/EOSM_202.sym luôn đi đúng cặp. Đừng thay một
  trong hai bằng file từ thẻ khác.
- Thẻ này không có filmroll.mo. Đừng chép filmroll.mo vào: hai module tranh
  nhau cùng một điểm hook, và WindyFilm sẽ báo "Disable filmroll.mo".
- ML/SETTINGS/WINDYFLM.EN là file bật module lúc khởi động.

Cài đặt máy: JPEG L (kích thước M / S không được xử lý), LiveView chụp ảnh.
Chỉ RAW thì không có JPEG nên không có màu phim (RAW+L thì có).


2. DÙNG
-------
- Menu ML: Shoot > WindyFilm. Mở trình chọn phim bằng "Open film browser",
  hoặc giữ lâu nút DOWN khi đang ở LiveView.
- Trong trình chọn phim:
    Đổi phim ............. trái / phải
    Chọn hàng ............ lên / xuống
    Đổi giá trị hàng ..... vòng xoay sau
    Nạp phim ............. SET
    Nhóm kế tiếp ......... chạm 1 ngón
    Trang chi tiết ....... INFO
    Đóng ................. MENU hoặc chạm 2 ngón
  Bấm nửa nút chụp: giao diện đóng, chụp như thường.
- Hàng Light: Auto (giữ WB của bạn), Day (WB theo phim), Cloudy, Tungsten.
  WB của bạn được trả lại khi về Auto hoặc khi tắt module.
- Hàng Print: Scan / Cine print / Paper (chỉ với phim có mô hình in).
- Output khác: Log (5 đường cong), Log + LUT (cube trong ML/data, xem
  README_FILMLUT.txt), Off.
- Input curve (cho Log + LUT và lon Film LUT): sRGB, Canon Film 1 hoặc Canon
  Neutral. Module đảo đúng đường cong của style đó, đo từ ảnh RAW+L của bạn:
    Canon Film 1:  nạp file Film 1 vào User Def. 1, Contrast -2, rồi chọn
                   Picture Style User Def. 1.
    Canon Neutral: Picture Style Neutral, Contrast 0.
  Nếu máy khác điều kiện đó, module tự đọc sRGB và menu báo lý do ("Picture
  Style is ..., not ...", "User Def. 1 is a built-in style ...", "Style contrast is
  ...").
- Xem trước trên LiveView: cần bật Overlay > Global Draw của ML.


3. LẦN CHẠY ĐẦU: CẦN XEM
------------------------
1. ML/LOGS/WINDYFLM.LOG sau khi bật máy phải có:
     hook NN ff169414 rom e92d41fc err 0
     hooks: installed (0)
     pack ok: 49 tables
2. Chụp một ảnh mỗi chế độ: phim (Print Scan / Cine / Paper), Log, Log + LUT,
   một lon Film LUT. Mỗi ảnh có dòng "yuv NN ..." trong log.
3. Light Day / Cloudy / Tungsten rồi về Auto: WB trở lại như cũ.
4. Các phím của trình chọn phim; màu giao diện khi LiveView ở phía sau.
5. EXIF (UserComment) của ảnh có dòng "WindyFilm: ...".
6. Picture Style Film 1 (User Def. 1, Contrast -2), Log + LUT, Input curve
   Canon Film 1: dòng "lut ..." trong log có "in-film1 b41 c-2" (b = style nền
   của User Def. 1, c = Contrast), EXIF có "In Canon Film 1". Rồi Picture Style
   Neutral (Contrast 0), Input curve Canon Neutral: "in-neutral ... c0". Chụp
   RAW+L cùng lúc để so với RAW.


4. RAW PROBE (P0): CHỈ ĐỂ THỬ
-----------------------------
Mục đích: biết lúc module xử lý ảnh, RAW 14-bit của Canon còn nguyên trong RAM
hay không (cần cho đường RAW sau này). Probe chỉ ghi lại, KHÔNG đổi ảnh.

- Shoot > WindyFilm > RAW probe: On. "On + ROM call" gọi thêm một hàm tra cứu
  của Canon (lần chạy 14/11 cho thấy an toàn).
- Bật một phim (Output = phim, không phải Off) để module xử lý ảnh với thời
  gian thật. Đặt chất lượng RAW+L.
- Ngay sau khi bật máy: 1 ảnh đơn. Rồi thêm 2 ảnh đơn, một loạt chụp liên tục
  5 ảnh, một loạt 2 ảnh. Có thể lặp lại với "On + ROM call".
- Bản này ghi thêm buffer RAW thứ hai (ảnh cuối của loạt chụp nằm ở đó) và
  ảnh đầu sau khi bật máy.
- Gửi lại: ML/LOGS/WINDYFLM.LOG, ML/LOGS/WFPROBE.BIN và các file CR2 + JPG của
  những ảnh đó. Log giờ ghi nối tiếp, không cần xoá trước; WFPROBE.BIN vẫn làm
  mới ở ảnh probe đầu tiên của mỗi lần bật máy (ngay khi máy ghi xong ảnh đó),
  nên gửi nó trước khi tắt / bật lại máy để thử tiếp.
- Khi thẻ đang bận ghi ảnh, module chờ và ghi lại sau; log có dòng "probe:
  record #N not written ... yet: <bước> rc <mã>" rồi "written ... on try K".
  Bước "busy" nghĩa là máy còn đang ghi ảnh; đó là bình thường.
- Probe tự tắt mỗi lần bật máy (không lưu vào cấu hình).


5. FILE TRÊN THẺ
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
