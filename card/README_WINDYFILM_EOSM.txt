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
- SET nạp phim: hiện hoạt cảnh nạp phim khoảng 1 giây, rồi giao diện tự đóng
  và về LiveView. Bấm phím bất kỳ trong lúc hoạt cảnh để đóng ngay. Phim đã
  nạp rồi hoặc lỗi thì giao diện vẫn mở như cũ, kèm dòng báo.
- Nếu giao diện không lên được ngay (ví dụ máy đang xử lý ảnh vừa chụp), sau
  2 giây các phím lại về Canon / menu ML: chạm 2 ngón vẫn mở menu ML.
- Hàng Light: Auto (giữ WB của bạn), Day (WB theo phim), Cloudy, Tungsten.
  WB của bạn được trả lại khi về Auto hoặc khi tắt module.
- Hàng Print: Scan / Cine print / Paper (chỉ với phim có mô hình in).
- Hàng Halation / Bloom (từ 15/11, hai hàng riêng; mặc định Film + Normal): Halation = viền đỏ cam quanh
  điểm sáng (ánh sáng dội từ đế phim): Off / Film / Spektra. Bloom = quầng sáng mềm
  cùng màu với chính điểm sáng: Off / Low / Normal / Strong. Bật riêng từng cái được
  (Bloom không cần Halation và ngược lại); tắt cả hai thì không tốn thời gian. File
  cài đặt cũ: "Film + bloom" thành Film + Bloom Normal, "Spektra + bloom" thành
  Spektra + Bloom Normal. Cũng có trong menu ML (mục Bloom ngay dưới Halation).
- Hàng Highlights / Shadows: -5 .. +5, mặc định 0. Dương làm sáng vùng sáng /
  nâng vùng tối, âm làm tối. Áp cho phim và Log + LUT (cả lon Film LUT), không
  áp cho Log phẳng. Cũng có trong menu ML. Trình chọn giờ có 10 hàng: hiện 7,
  danh sách tự cuộn theo hàng đang chọn (thanh cuộn nhỏ bên phải).
- Output khác: Log (5 đường cong), Log + LUT (cube trong ML/data, xem
  README_FILMLUT.txt), Off.
- Lon Leica Vivid (nhóm Film LUT, cần ML/data/LEICAVSR.CUB): look "L Leica 2"
  của Lumix đặt lên ảnh Picture Style Standard, như preset Real Time LUT của
  S9 (Standard + LUT). Đây KHÔNG phải look cho log: cube áp thẳng lên màu sRGB
  của JPEG Standard, không qua V-Log hay log nào. Menu ghi "Leica Vivid (look
  on Standard)". Nạp lon thì module chuyển Picture Style sang Standard
  (Neutral cũng chạy, màu nhạt hơn). Lon này luôn đọc JPEG như sRGB, bất kể
  Input curve và Log curve, và không tự bù sáng (LUT EI vẫn chỉnh tay được).
  Lần đầu nạp cube mất vài giây, sau đó dùng cache .FRT.
- 13 lon "Tung" cũ giờ tên "Windy ...": Windy Neutral, Windy EKTA P,
  Windy 250D P / M, Windy 500T P / M, Windy EKTA M, Windy EKTAa P / M,
  Windy EXR P / M, Windy EXRa P / M.
- 12 lon Windy của CinePrint (tất cả trừ Windy Neutral) giờ dùng cube
  Canon Log 2 thay cho V-Log, cho đúng log của Canon. Nạp lon thì Log curve
  thành Canon Log 2 và cube là ML/data/<mã>C2.CUB: EKTAP_C2, T250DPC2,
  T250DMC2, T500TPC2, T500TMC2, TEKTAMC2, TEKAAPC2, TEKAAMC2, TEXRP_C2,
  TEXRM_C2, TEXRAPC2, TEXRAMC2. Đây là cube 65^3 xuất từ PowerGrade C-Log2
  của CinePrint (file "C-Log2 <look> 65.cube"), màu dày hơn bản OSM.
  Thiếu cube nào thì lon đó báo "LUT cube not on the card".
  Windy EKTA P giờ là PowerGrade "C-Log2 EKTA P" của CinePrint: lon KHÔNG
  còn nạp EKTAP_VL.CUB (bản "Tung EKTA P V-Log" bạn xuất). File đó cứ để
  trên thẻ: hàng Film LUT với Log curve V-Log chọn tay vẫn dùng nó.
  Các cube V-Log cũ (T250DPVL.CUB ...) cũng vậy: chọn tay Log curve V-Log
  ở hàng Film LUT thì module dùng cube V-Log nếu có trên thẻ.
  Windy Neutral vẫn là V-Log (TUNGNTVL.CUB).
  Lần đầu nạp một cube 65^3 mất khoảng 20 giây trên EOS M (cube 33^3 khoảng
  5 giây): đợi menu Film LUT hết báo "Loading" rồi hãy chụp. Sau đó module
  nạp từ cache .FRT, nhanh như cũ (cache V-Log cũ không dùng lại). Đổi LUT
  EI hoặc Input curve thì cube được nạp lại từ đầu. Chép cube khác đè đúng
  tên (ví dụ bản OSM 33^3) cũng chạy, không cần module mới.
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


5. RAW DEVELOP "B" (CHẬM): BẢN THỬ
----------------------------------
Mục đích: màu phim làm từ chính file RAW 14-bit của Canon (nhiều chi tiết
vùng sáng / tối hơn JPEG), thay cho JPEG 8-bit của Canon. Mỗi ảnh mất khoảng
30 giây. Chưa chạy trên máy thật lần nào: đây là lần thử đầu tiên.

Bật:
- Shoot > WindyFilm > RAW develop: "On (slow)". Mỗi lần bật máy nó tự về Off.
- Chất lượng ảnh: RAW + L (JPEG L Fine). M / S RAW, chỉ JPEG hoặc chỉ RAW thì
  không develop (ảnh có màu phim thường như cũ).
- Tỉ lệ khung 3:2. Drive: chụp đơn hoặc hẹn giờ. Chụp liên tục thì mọi ảnh
  có màu phim thường (8-bit), log ghi lý do.
- Highlights / Shadows có tác dụng với phim. Với Log + LUT khi RAW develop
  đang bật thì chưa có tác dụng (bản thử này).
- Lon Leica Vivid (mới từ thẻ RAWB3): RAW develop cũng develop Leica từ RAW:
  RAW ra sRGB rồi áp thẳng cube gốc "L Leica 2" 33^3 lên mã sRGB, không qua
  V-Log. Cần file ML/DATA/LEICAVSR.CUB (cube gốc, đã có trên thẻ).
  Highlights / Shadows có tác dụng; grain / halation như phim.
- Bảng RAW develop của lon Log + LUT / Leica (mới): lần đầu dựng từ cube (cube
  65^3 của CinePrint lâu hơn, có thể vài chục giây), rồi lưu ở ML/DATA/<tên
  cube>.RBT (356 KB). Những lần sau (bật máy, đổi phim, xem ảnh xong) đọc file
  đó, khoảng 1 giây. Log ghi "... .RBT hit" hoặc lý do dựng lại. Xoá file .RBT
  thì lần sau dựng lại, không sao.
- Khi RAW develop Off, thẻ này chạy giống thẻ EOSM_WindyFilm (cùng bản main,
  có RAW probe v2 và Highlights / Shadows).
- Dòng menu báo trạng thái: đang nạp bảng, sẵn sàng ("About 30 s per shot"),
  thiếu bộ nhớ, hoặc chất lượng / drive chưa đúng.

Chụp:
- Chụp từng tấm. Sau khi bấm, màn hình hiện "RAW develop: preparing" rồi
  "RAW develop NN%". Chờ tới khi hết thông báo (khoảng 30 giây). Trong lúc đó
  máy đang bận xử lý ảnh, chưa chụp tấm sau được: đó là bình thường.
- Muốn dừng: giữ nửa nút chụp khoảng 2 giây. JPEG của tấm đó sẽ có phần trên đã
  develop (chưa có màu phim), phần dưới là JPEG gốc của Canon. File CR2 không
  bao giờ bị đổi. Quá 90 giây thì module tự dừng.
- Tắt máy giữa chừng: module dừng trong khoảng 1 giây rồi để máy ghi ảnh và tắt.
  Việc này CHƯA thử trên máy thật; nếu máy treo (không tắt, không phản hồi)
  thì tháo pin ra lắp lại, và ghi lại lúc đó đang ở bao nhiêu %.
- Nếu RAW trong RAM không khớp với ảnh vừa chụp, module KHÔNG develop mà dùng
  màu phim thường; log ghi "rawb: 8-bit (...)" cùng lý do.

Cần thử và gửi lại:
1. Phim (Output Film), 3-4 tấm đơn: cảnh có trời sáng / đèn bị cháy sáng, và
   cảnh tối. Mỗi cảnh chụp thêm 1 tấm với RAW develop Off để so sánh.
2. 1-2 tấm với Output Log và Log + LUT.
3. Một lần dừng bằng nửa nút chụp, và (nếu dám) một lần tắt máy giữa chừng.
4. Một loạt chụp liên tục 3 tấm khi RAW develop On (phải ra màu phim thường).
5. 1-2 tấm lon Leica Vivid với RAW develop On, kèm cùng cảnh với RAW develop Off.
Gửi: ML/LOGS/WINDYFLM.LOG và các cặp CR2 + JPG của những tấm đó (cả tấm Off).
Mỗi tấm có các dòng "rawb: ..." trong log: bộ đệm RAW đã chọn, điểm khớp,
mức đen / trắng và thời gian từng bước.


6. FILE TRÊN THẺ
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
