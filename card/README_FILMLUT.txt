FILM LUT CHO CHẾ ĐỘ LOG + LUT CỦA WINDYFILM
==========================================

Dữ liệu film của 4 bộ cube V250D / V500T / PRT400 / FP400H:
spektrafilm profiles, CC BY-SA 4.0, A. Volpato (github.com/andreavolpato/spektrafilm).

Các cube đó được tính từ dữ liệu film của spektrafilm, nên chính chúng cũng là
CC BY-SA 4.0. Khi chia sẻ: giữ dòng ghi công trên, kèm SPEKTRAFILM_LICENSE.txt
và CHANGELOG_FILMLUT.txt (những gì đã thay đổi so với dữ liệu gốc). Code tạo
cube là code riêng của WindyStrife, viết từ đầu; không dùng code của
spektrafilm (GPLv3).

Các cube còn lại trên thẻ (EKTAP, EKTAR5, FJ3513, KD83, LBONO, LEICAV, T250D,
T500T, TEKAA, TEKTA, TEXRA, TEXRM, TEXRP, TUNGNT) là grade riêng của chủ máy,
chỉ để dùng trên thẻ này. Các lon TUNGNT / EKTAP / T... giờ tên "Windy ..."
(Windy Neutral, Windy EKTA P, Windy 250D P, ...); tên file không đổi. LEICAV là
lon "Leica Vivid": cube hiển thị, xem mục 2.


1. TÊN FILE
-----------
<MÃ PHIM, đệm "_" cho đủ 6 ký tự><hậu tố>.CUB, ví dụ V250D_VL.CUB, PRT400S3.CUB.

  Hậu tố   Chế độ trên máy
  S3       S-Log3 + LUT
  VL       V-Log + LUT      (các lon Film LUT dùng bản VL)
  LC       LogC3 + LUT
  C2       Canon Log 2 + LUT
  C3       Canon Log 3 + LUT

Mỗi cube chỉ dùng cho đúng chế độ của nó; dùng sai chế độ thì màu và độ sáng
sẽ sai. SLOG3.CUB / VLOG.CUB / LOGC3.CUB (và CLOG2 / CLOG3 nếu có) là LUT kỹ
thuật "Technical": log -> Rec.709.


2. DÙNG TRÊN MÁY
----------------
- Cube nằm trong ML/data/ (giữ nguyên tên, chữ hoa 8.3).
- Shoot > WindyFilm > Output: "Log + LUT", chọn Log curve, rồi Film LUT.
  Hoặc trong trình chọn phim, nạp một lon ở nhóm Film LUT: module tự đặt
  V-Log + LUT với cube của lon đó. Lon chỉ hiện khi cube _VL của nó có trên thẻ.
- Lần đầu dùng mỗi cube, module nạp vài giây rồi lưu bảng đã tính vào
  ML/data/<tên>.FRT; các lần sau nạp từ cache. Đợi menu Film LUT hết báo
  "Loading" rồi hãy chụp. Chép đè cube mới thì .FRT tự tạo lại.
- Cài đặt máy: Picture Style Neutral, Color space sRGB, JPEG L.
- Riêng Leica Vivid (LEICAVSR.CUB): cube hiển thị làm trên ảnh Picture Style
  Standard (sRGB vào, sRGB ra). Module áp nó thẳng lên mã sRGB của JPEG, không
  qua V-Log, nên dùng được với mọi Log curve. Nạp lon thì module tự chuyển
  sang Standard, bỏ qua Input curve và không tự bù sáng (LUT EI chỉnh tay).
- Cube không có grain hay halation. Muốn có thì bật Grain / Halation của
  WindyFilm: chúng chạy sau LUT.
