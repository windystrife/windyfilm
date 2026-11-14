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
  hoặc bấm TRASH (không mở trong chế độ xem ảnh, nơi TRASH là xoá). Có
  LiveView hay ngắm qua kính đều mở được. Ngắm qua kính thì màn hình tự bật
  lên như khi mở menu ML (có thể thấy menu Canon loé lên một chút).
- Trong trình chọn phim:
    Đổi phim ............. joystick trái / phải
    Chọn hàng ............ joystick lên / xuống
    Đổi giá trị hàng ..... bánh xe trên hoặc bánh xe sau (cả khi có LiveView)
    Sửa hàng bằng joystick nhấn joystick, rồi trái / phải đổi giá trị;
                           nhấn joystick lần nữa để thôi (thanh phím dưới
                           cùng ghi "JOY Edit" / "JOY Done")
    Nạp phim ............. SET
    Nhóm kế tiếp ......... Picture Style
    Trang chi tiết ....... INFO
    Đóng ................. MENU hoặc TRASH
  Bấm nửa nút chụp: giao diện đóng, chụp như thường.
- SET nạp phim: hiện hoạt cảnh nạp phim khoảng 1 giây, rồi giao diện tự đóng
  và về LiveView. Mở từ kính ngắm (không LiveView) thì máy tự bật LiveView sau
  khi nạp. Bấm phím bất kỳ trong lúc hoạt cảnh để đóng ngay; bấm nửa nút chụp
  thì đóng và không bật LiveView. Phim đã nạp rồi hoặc lỗi thì giao diện vẫn
  mở như cũ, kèm dòng báo.
- Hàng Light: Auto (giữ WB của bạn), Day (WB theo phim), Cloudy, Tungsten.
  WB của bạn được trả lại khi về Auto hoặc khi tắt module.
- Hàng Print: Scan / Cine print / Paper (chỉ với phim có mô hình in).
- Hàng Highlights / Shadows: -5 .. +5, mặc định 0. Dương làm sáng vùng sáng /
  nâng vùng tối, âm làm tối. Áp cho phim và Log + LUT (cả lon Film LUT), không
  áp cho Log phẳng. Cũng có trong menu ML. Trình chọn giờ có 9 hàng: hiện 7,
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
  Neutral. Hai đường cong đo trên EOS M (Film 1: file trong User Def. 1,
  Contrast -2; Neutral: Contrast 0); trên 5D2 chưa kiểm, cứ để sRGB nếu không
  chắc.
- Xem trước trên LiveView: cần bật Overlay > Global Draw của ML.
- RAW probe (Shoot > WindyFilm): chỉ để thử, xem mục 4.


3. LẦN CHẠY ĐẦU: CẦN XEM
------------------------
1. ML/LOGS/WINDYFLM.LOG sau khi bật máy phải có dòng hook TOP / BOT với err 0,
   "hooks: installed (0)" và "pack ok: 49 tables".
2. Chụp một ảnh mỗi chế độ: phim (Print Scan / Cine / Paper), Log, Log + LUT,
   một lon Film LUT. Mỗi nửa ảnh có dòng "yuv TOP ..." / "yuv BOT ..." trong log.
3. Light Day / Cloudy / Tungsten rồi về Auto: WB trở lại như cũ.
4. Các phím của trình chọn phim; màu giao diện khi LiveView ở phía sau.
5. EXIF (UserComment) của ảnh có dòng "WindyFilm: ...".


4. RAW PROBE (P0): CHỈ ĐỂ THỬ
-----------------------------
Mục đích: biết lúc module xử lý mỗi nửa ảnh (TOP / BOT), RAW 14-bit của Canon
còn nguyên trong RAM hay không, và mỗi nửa ảnh nằm ở đâu trong RAW (cần cho
đường RAW sau này). Probe chỉ ghi lại, KHÔNG đổi ảnh. Mỗi ảnh cho hai bản ghi
(TOP và BOT).

1. Cảnh nhiều chi tiết (lá cây, gạch, chữ; tránh trời trơn), ISO 100-400, chân
   máy hoặc tì chắc. Bật một phim (Output = phim) và đặt chất lượng RAW+L.
   Shoot > WindyFilm > RAW probe: On.
2. Ba ảnh đơn, cách nhau khoảng 5 giây.
3. Một loạt chụp liên tục 5 ảnh.
4. RAW probe: On + ROM call (gọi thêm vài hàm tra cứu của Canon). Một ảnh đơn,
   rồi một loạt 3 ảnh.
5. (Tuỳ chọn, để biết mức trắng) mỗi ISO 100, 200, 400, 800, 1600, 3200 một
   ảnh dư sáng khoảng 3 EV vào tường trắng hoặc trời (chỉ RAW cũng được).
6. Gửi lại: ML/LOGS/WINDYFLM.LOG, ML/LOGS/WFPROBE.BIN và mọi file CR2 + JPG của
   buổi chụp. WFPROBE.BIN làm mới ở lần probe đầu tiên của mỗi lần bật máy, nên
   gửi nó trước khi tắt / bật lại máy để thử tiếp.

- Khi thẻ đang bận ghi ảnh, module chờ và ghi lại sau; log có dòng "probe:
  record #N not written ... yet: <bước> rc <mã>" rồi "written ... on try K".
  Bước "busy" nghĩa là máy còn đang ghi ảnh; đó là bình thường.
- Probe tự tắt mỗi lần bật máy (không lưu vào cấu hình).


5. RAW DEVELOP "B" (CHẬM): BẢN THỬ, ĐÃ PROBE, CHƯA THỬ DEVELOP TRÊN MÁY
------------------------------------------------------------------------
Mục đích: màu phim làm từ chính file RAW 14-bit của Canon thay cho JPEG 8-bit.
Mỗi ảnh khoảng 35-45 giây (nửa trên rồi nửa dưới). Develop chưa chạy trên máy
thật.

RAW probe trên máy (14/11, thư mục 5D2_probe_ketqua): ở cả nửa trên và nửa
dưới, RAM có đúng RAW của tấm vừa chụp. Địa chỉ cũ lệch một hàng (đọc từ hàng
1 của CR2); thẻ RAWB3 đọc từ hàng 0 nên đã đúng. RAW develop trên thẻ này thử
được, nhưng chưa ai thử trên máy. Thẻ 5D2_WindyFilm_RAWB2 còn địa chỉ cũ:
dùng thẻ đó thì để RAW develop Off.
Mỗi ảnh module vẫn so RAW với JPEG của Canon trước khi develop; không khớp thì
ảnh ra màu phim thường (8-bit) và log ghi "rawb: 8-bit (buffer nomatch ...)"
kèm điểm số.

Trình chọn phim (mới từ thẻ RAWB3): mở được cả khi ngắm qua kính, không cần
LiveView (mục 2). Bấm TRASH mà không mở thì log ghi "ui: TRASH not taken: ...".

Bật:
- Shoot > WindyFilm > RAW develop: "On (slow)". Mỗi lần bật máy nó tự về Off.
- Chất lượng ảnh RAW + L (JPEG L Fine). Drive: chụp đơn hoặc hẹn giờ. Chụp liên
  tục, M / S RAW, chỉ JPEG: màu phim thường như cũ.

Chụp:
- Chụp từng tấm, chờ hết thông báo "RAW develop NN%" (hiện hai lần: nửa trên,
  nửa dưới). Trong lúc đó máy bận, chưa chụp tấm sau được: bình thường.
- 8 điểm ảnh sát mép trái và phải mỗi ảnh vẫn là ảnh của Canon (màu phim
  thường); phần còn lại là RAW develop.
- Dừng: giữ nửa nút chụp khoảng 2 giây. Nửa trên dừng giữa chừng thì cả ảnh
  không có màu phim (phần đã develop + phần JPEG gốc). CR2 không bao giờ bị đổi.
- Tắt máy giữa chừng: module dừng trong khoảng 1 giây. CHƯA thử trên máy thật;
  nếu máy treo thì tháo pin, và ghi lại lúc đó đang ở bao nhiêu %.
- Highlights / Shadows có tác dụng với phim; với Log + LUT khi bật RAW develop
  thì chưa.
- Lon Leica Vivid (mới từ thẻ RAWB3): RAW develop cũng develop Leica từ RAW:
  RAW ra sRGB rồi áp thẳng cube gốc "L Leica 2" 33^3 lên mã sRGB, không qua
  V-Log. Cần file ML/DATA/LEICAVSR.CUB (cube gốc, đã có trên thẻ).
  Highlights / Shadows có tác dụng; grain / halation như phim.

Gửi lại: ML/LOGS/WINDYFLM.LOG và các cặp CR2 + JPG (kèm 1 tấm cùng cảnh với
RAW develop Off). Log có các dòng "rawb: ..." cho từng nửa ảnh: địa chỉ RAW,
điểm khớp, mức đen / trắng, thời gian từng bước.


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
ML/LOGS/WFPROBE.BIN       bản ghi của RAW probe (chỉ khi bật probe)
MD5SUMS.txt               md5 của mọi file khi làm thẻ
