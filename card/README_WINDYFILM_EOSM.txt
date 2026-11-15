WindyFilm cho Canon EOS M (firmware 2.0.2) - thẻ thử đầu tiên, 14/11/2026
=========================================================================

Module Magic Lantern tạo màu phim cho ảnh JPEG ngay lúc chụp.
Tác giả: WindyStrife. Giấy phép GPL-2.0 (LICENSE_WINDYFILM.txt).
Dữ liệu và thông số bên thứ ba: ATTRIBUTIONS.txt.
Bảng Film stage (lon G00..G44 trong WFFILMS.DAT, WFSTAGE.DAT và WFLOG.DAT) là dữ liệu
dẫn xuất (DESIGN 1a): chỉ dùng riêng, không chia sẻ.

Thẻ 15/11 tối (nhánh chain-log, phương án (c) bước 2): lon Film stage khi RAW develop
On lấy bảng log S-Log3 của đúng cặp âm bản x print từ ML/DATA/WFLOG.DAT, nên cả màu
phim nằm trong bước develop từ RAW (vùng sáng giữ tới mức cháy của cảm biến). Đường
JPEG YUV 8-bit 4:2:2 vẫn như cũ khi RAW develop Off. Xem mục 5 (lon Film stage trên
RAW) và bước 6 trong "Cần thử". G09 Double-X 5222 giờ là phim đen trắng, chỉ in 2302.

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
- Hàng Print stock (trước gọi là Print): Scan / Cine print / Paper với phim có mô
  hình in; với lon Film stage là 15 print stock (mục 6).
- Hàng Grain: Off / Fine / Normal / Strong (độ hạt đổi theo độ sáng của vùng ảnh) và
  Even Fine / Even Normal / Even Strong (hạt như nhau ở mọi độ sáng). Mặc định Normal.
- Hàng Halation / Bloom (từ 15/11, hai hàng riêng; mặc định Wide + Normal):
  Halation = viền đỏ cam quanh điểm sáng (ánh sáng dội từ đế phim): Off / Wide /
  Tight / Wrap. Wide = quầng rộng, đo theo từng phim (setting cũ "Film"). Tight =
  quầng hẹp, sát điểm sáng (setting cũ "Spektra"). Wrap = ánh sáng tràn qua mép vật
  sáng sang vùng tối kề bên; Wrap không đi cùng Bloom.
  Bloom = quầng sáng mềm cùng màu với chính điểm sáng: Off / Low / Normal / Strong.
  Bật riêng từng cái được (Bloom không cần Halation và ngược lại); tắt cả hai thì
  không tốn thời gian. File cài đặt cũ: Film thành Wide, Spektra thành Tight, "Film +
  bloom" thành Wide + Bloom Normal, "Spektra + bloom" thành Tight + Bloom Normal.
  Cũng có trong menu ML (mục Bloom ngay dưới Halation). Halation và Bloom chỉ chạy
  với ảnh 3:2.
- Hàng Gauge: 16 mm / 35 mm / 65 mm (mặc định 35 mm = như trước). Khổ phim giả lập:
  16 mm hạt thô hơn và quầng halation / bloom rộng hơn; 65 mm hạt mịn hơn, quầng hẹp
  hơn. Chỉ có tác dụng khi Grain, Halation hoặc Bloom đang bật. EXIF ghi "| Gauge
  16 mm" (không ghi khi 35 mm).
- Hàng Highlights / Shadows: -5 .. +5, mặc định 0. Dương làm sáng vùng sáng /
  nâng vùng tối, âm làm tối. Áp cho phim và Log + LUT (cả lon Film LUT), không
  áp cho Log phẳng. Cũng có trong menu ML. Trình chọn giờ có 18 hàng (7 hàng cuối
  chỉ dùng với lon Film stage, mục 6; hàng không dùng được thì mờ): hiện 7, danh
  sách tự cuộn theo hàng đang chọn (thanh cuộn nhỏ bên phải).
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
     pack ok: 706 tables, index NNms, catalog 97 films
     stage ok: 657 of 657 pairs match the pack (44 negatives, 15 prints), index NNms
   (dòng "stage ok" là của Film stage, xem mục 7).
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
- Lon Film stage (mới, 15/11 tối): RAW develop đọc bảng log 33^3 (215 KB) của đúng
  cặp âm bản x print từ ML/DATA/WFLOG.DAT, develop RAW ra S-Log3 rồi áp bảng đó, nên
  cả màu phim nằm trong develop; sau đó chỉ còn grain / halation / bloom / mist.
  Không có file .RBT: mỗi lần đổi phim / print (hoặc bật lại RAW develop) đọc lại
  215 KB từ thẻ. Log:
    rawb: tables log pack G08 v4 c1 NNms (offset ..., 643 entries, ...)
  Ghi lại NN: đó là thời gian đọc thẻ + dựng bảng (chưa đo trên máy thật).
- Vai sáng của lon Film stage trên RAW (15/11, chủ máy chọn i): đèn và chỗ sáng giữ
  màu tới trắng của phim rồi mới chuyển dần sang kem / trắng. Muốn ảnh y như thẻ
  RAWB14 / RAWB15 (từng kênh tự cháy sáng, vai sáng a) thì sửa trên máy tính file
  ML/SETTINGS/WINDYFLM.CFG thành dòng shoulder=a (shoulder=d: giữ nguyên tông màu
  tới hết, để so), shoulder=i là mặc định. Lon Log + LUT và phim thường không đổi. Log mỗi tấm:
    rawb: black ..., roll 1 c.. y.., shoulder i NNms
  (NN = thời gian dựng bảng vai sáng của tấm đó).
- Tạm thời (chờ quyết định): bảng log chưa chứa các hàng knob, Strength hay
  Highlights / Shadows. Một trong các hàng đó khác mặc định (knob khác 0 / Off /
  100 %, Strength khác 100 %, Highlights / Shadows khác 0) thì lon Film stage đi
  đường 8-bit như trước: develop RAW ra sRGB rồi áp bảng phim 8-bit. Log ghi lý do:
    rawb: chain film G08 v4 on the 8-bit chain (strength)
  (hoặc "knob rows", "highlights / shadows"). Print nào không có trong WFLOG.DAT
  hoặc file hỏng / thiếu: một dòng "rawb: log pack G08 v4: ..." rồi cũng đường 8-bit.
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
6. (15/11 tối) Lon Film stage trên RAW: nạp G08 (Vision3 5219 500T), Print stock
   2383, mọi knob mặc định, Strength 100 %, Highlights / Shadows 0. Bật RAW develop
   On. Log phải có "rawb: tables log pack G08 v4 c1 NNms": ghi lại NN. Chụp 2 tấm:
   một cảnh có trời sáng / đèn cháy sáng, một cảnh tối; mỗi cảnh thêm 1 tấm RAW
   develop Off để so. Đổi Print stock 2393, rồi 2302 (ảnh ĐEN TRẮNG): mỗi lần đổi có
   một dòng "tables log pack" mới, ghi lại NN. Rồi Strength 30 %: log phải có
   "on the 8-bit chain (strength)", chụp 1 tấm. Thêm 1 tấm G09 (Double-X, chỉ 2302).
Gửi: ML/LOGS/WINDYFLM.LOG và các cặp CR2 + JPG của những tấm đó (cả tấm Off).
Mỗi tấm có các dòng "rawb: ..." trong log: bộ đệm RAW đã chọn, điểm khớp,
mức đen / trắng và thời gian từng bước.


6. FILM STAGE: ÂM BẢN x PRINT STOCK (15/11)
-------------------------------------------
Vào: trong trình chọn phim, chạm 1 ngón để qua nhóm kế tiếp: Colour -> Cine -> B&W
-> Film LUT -> Film stage (nhóm cuối, biểu tượng lon tím). Nhóm có 44 lon, mã G00 ..
G44 (không có G33), vẽ theo màu hãng: Kodak Vision3 / Vision2 / Vision / EXR / ECN /
Double-X / Primetime, Fujifilm Eterna / Eterna Vivid / Reala / Super F / Color A.

Phim chuỗi (chain film): mỗi lon là một phim âm bản điện ảnh (ví dụ Vision3 5219
500T). Ảnh cuối = âm bản đó được in lên một print stock (phim in chiếu rạp), chọn ở
hàng Print stock. Mỗi cặp âm bản x print có bảng màu 17^3 riêng trong WFFILMS.DAT.
- ISO và nhiệt độ màu trên header là của âm bản thật: 33 phim T (tungsten) 3200 K,
  11 phim D (daylight) 5200 K, Double-X 5222 EI 250. Light Day với phim T đặt 3200 K:
  ảnh ngoài trời ngả xanh như phim tungsten không kính lọc. Light Auto giữ WB của
  bạn (giống gắn kính lọc 85).

Hàng Print stock: 15 print:
    2302      Kodak B&W Print Film 2302              (ĐEN TRẮNG)
    2383      Kodak Vision Color 2383                (MẶC ĐỊNH)
    2393      Kodak Vision Premier Color 2393
    2395      Kodak Vision Color Teleprint 2395 500T
    5380      Eastman Color Print LC 5380
    5381/50   Eastman Color Print 5381 (1950)
    5381/72   Eastman Color Print 5381 (1972)
    5382      Eastman Color Print 5382
    5384/82   Eastman Color Print 5384 (1982)
    5384/92   Eastman Color Print 5384 (1992)
    5385/93   Eastman Color Print LC 5385 (1993)
    5385/62   Eastman Color Print 5385 (1962)
    5386      Eastman EXR Color Print 5386
    5383      Eastman Color Print 5383
    3523XD    Fuji Eterna-CP 3523XD
- Mặc định là 2383 (bản in rạp phổ biến nhất): lon Film stage không có Scan. Khi
  hàng Print stock đang là Scan (mặc định của mọi phim) hoặc là print mà âm bản đó
  không có, máy dùng Kodak Vision Color 2383. Chỉ 2302 là bản in đen trắng.
- Không có Fuji Eterna-CP Type 3513D: chuỗi không có bảng của print này.
- Năm cặp cho ảnh giống hệt nhau trên mọi âm bản (cùng một bảng): 2383 = 2393,
  5380 = 5381/72, 2395 = 5384/82, 5384/92 = 5385/93, 5385/62 = 5386. Cả hai tên được
  giữ như danh sách gốc; thực tế có 10 kiểu in khác nhau.
- ECN-I 5248 25T (G17) chỉ có 12 print (thiếu 2302, 5383, 3523XD).
- Double-X 5222 (G09) là âm bản đen trắng: chỉ in 2302, hàng Print stock chỉ có 2302.
- Trình chọn hiện tên đầy đủ khi vừa chỗ; menu ML hiện mã, tên đầy đủ ở dòng help.
  EXIF ghi tên đầy đủ ("| Print Kodak Vision Color 2383").
- Grain, Halation, Bloom, Gauge, Mist, Strength, Highlights / Shadows dùng như phim
  thường (mục 2).

7 hàng cuối (hàng knob) chỉnh chính chuỗi âm bản -> print. Chỉ dùng được khi: lon
Film stage, Output Film, và có ML/DATA/WFSTAGE.DAT trên thẻ; không đủ thì hàng mờ.
    Film exp ....... -2 .. +2 stop, bước 1/3, mặc định 0. Phơi sáng âm bản: dương =
                     phơi dư (ảnh sáng hơn), âm = phơi thiếu (tối hơn).
    Push / pull .... Pull 1 .. Push 1, bước 1/3, mặc định 0. Trong mô hình này push /
                     pull chỉ là dịch phơi sáng, cộng dồn với Film exp.
    Exp comp ....... Off / On. On: bỏ Film exp và Push / pull (hai hàng mờ đi), giữ
                     phơi sáng đã khớp (xám 18 % ra đúng xám giữa).
    Printer R / G / B  -12 .. +12 điểm, mặc định 0; mỗi điểm 0,025 logE. Đèn in từng
                     màu như máy in phim: thêm điểm = thêm ánh sáng màu đó lên print,
                     bản in ngả về màu đối (R+ ngả xanh lơ, G+ ngả hồng tím, B+ ngả
                     vàng) và tối hơn chút. Cả ba cùng +: tối hơn, cùng -: sáng hơn.
    Interlayer ..... 0 .. 200 %, bước 25 %, mặc định 100 % (= của chuỗi). Hiệu ứng
                     giữa các lớp màu: thấp = màu nhạt và lẫn hơn, cao = màu tách và
                     đậm hơn.
- Để mặc định hết (0 / 0 / Off / 0 0 0 / 100 %) thì máy dùng bảng có sẵn trong
  WFFILMS.DAT như mọi phim, không đọc WFSTAGE.DAT.
- Đổi một hàng knob (hoặc đổi phim / print khi knob khác mặc định): máy dựng bảng mới
  từ WFSTAGE.DAT. Không đọc cả file 21,3 MB: lúc bật máy chỉ đọc mục lục (khoảng
  20 KB); mỗi lần dựng đọc khoảng 650 KB của đúng cặp đó (nửa âm bản 435 KB, rồi
  nửa print 217 KB, cần khoảng 470 KB bộ nhớ tạm, trả lại ngay), tính toán khoảng
  20 ms trên EOS M. Log: "stage G08 v4 composed NNms k..." (bảng dựng từ knob) hoặc
  "pack G08 v4 loaded NNms" (bảng có sẵn). G08 = mã lon, v4 = print (v3 = 2302, v4 =
  2383 .. v18 = 3523XD).
- Cấu hình ghi dòng "stage=fexp,push,comp,r,g,b,iie". EXIF thêm "| Film exp +1",
  "| Push 1/3", "| Exp comp", "| Printer R+6 G0 B0", "| Interlayer 50%" khi khác mặc định.
- Darkroom (cuộn phim) dùng cùng bảng và cùng các hàng: xem HUONGDAN_DARKROOM_EOSM.txt.


7. FILM STAGE: LẦN CHẠY ĐẦU
---------------------------
1. Thẻ phải có ML/DATA/WFSTAGE.DAT (21 323 868 B; md5 trong MD5SUMS.txt). Bật máy,
   WINDYFLM.LOG phải có:
     pack ok: 692 tables, index NNms, catalog 97 films
     stage ok: 643 of 643 pairs match the pack (44 negatives, 15 prints), index NNms
   Ghi lại hai số "index NNms" (thời gian đọc mục lục). Nếu thay vào đó là
   "stage ML/DATA/WFSTAGE.DAT missing (-1): film-stage knob rows hidden" thì thẻ thiếu
   file; "... bad (-N) ..." là file hỏng hoặc khác bản: chép lại và kiểm MD5SUMS.txt.
   Thiếu file thì lon Film stage vẫn chụp được, chỉ mất 7 hàng knob.
2. Nhóm Film stage: lon có màu hãng, header đúng phim (ví dụ G08 Vision3 5219 500T:
   ISO 500, 3200 K). Nạp G08, để Print stock mặc định: hàng hiện "Kodak Vision Color
   2383". Chụp 1 tấm: ảnh màu. Log "pack G08 v4 loaded NNms".
3. Print stock 2302: chụp, ảnh ĐEN TRẮNG, log "pack G08 v3 loaded". Rồi 2393, cùng cảnh
   với tấm ở bước 2: ảnh phải giống hệt tấm 2383 (cặp trùng). Trong hàng Print stock
   không có 3513D.
4. Print stock về 2383, Film exp +1, chụp. Log
   "stage G08 v4 composed NNms k...": ghi lại NN (dự tính
   khoảng 20 ms tính + thời gian đọc khoảng 650 KB từ thẻ). Ảnh sáng hơn tấm ở bước 2.
   EXIF có "| Film exp +1". Chụp thêm 1 tấm ngay sau: không dựng lại (không có dòng
   composed mới).
5. Film exp về 0, Printer R +6: ảnh ngả xanh lơ so với bước 2 (ghi lại nếu ngả đỏ).
   EXIF "| Printer R+6 G0 B0". Printer R về 0, Printer B +6: ngả vàng.
6. Film exp +1, Exp comp On: Film exp và Push / pull mờ đi, ảnh như bước 2; EXIF có
   "| Exp comp".
7. Exp comp Off, Film exp 0, Interlayer 0 % rồi 200 %: màu nhạt rồi đậm hơn.
8. Grain Normal, Halation Wide, Gauge 16 mm: hạt to hơn, quầng rộng hơn so với 35 mm;
   65 mm: mịn hơn, quầng hẹp hơn. EXIF "| Gauge 16 mm".
9. Bộ nhớ: lặp bước 4 với LiveView đang bật, và một lần với RAW develop On. Nếu log có
   "stage: no memory for a half (N B) in 3000ms: film-stage knobs dropped" thì máy bỏ
   các hàng knob và dùng bảng mặc định: ghi lại lúc đó đang làm gì.
10. Đổi nhanh nhiều hàng knob rồi chụp ngay: ghi lại tấm đầu có phải chờ không.
11. Tắt máy bật lại: 7 hàng knob giữ giá trị đã đặt.
Gửi: ML/LOGS/WINDYFLM.LOG, ML/SETTINGS/WINDYFLM.CFG và các JPG (kèm CR2 nếu chụp RAW+L).


8. FILE TRÊN THẺ
----------------
ML/modules/windyflm.mo    module
ML/data/WFFILMS.DAT       bảng màu các phim (Scan / Cine / Paper, và 643 cặp Film stage)
ML/data/WFSTAGE.DAT       hai nửa âm bản / print của 643 cặp Film stage (21,3 MB),
                          cho 7 hàng knob (mục 6); thiếu thì các hàng đó mờ.
                          Dữ liệu dẫn xuất, chỉ dùng riêng
ML/data/WFLOG.DAT         bảng log S-Log3 của 643 cặp Film stage cho RAW develop
                          (92,5 MB, 428 bảng khác nhau, mục 5); thiếu thì lon Film
                          stage trên RAW đi đường 8-bit. Dữ liệu dẫn xuất, chỉ dùng riêng
ML/data/WFCANS.DAT        hình lon phim
ML/data/WFFONTS.DAT       font chữ của giao diện (Ubuntu Sans, UBUNTU_FONT_LICENCE.txt)
ML/data/WFPRINT.DAT       font chữ in dưới ảnh của Darkroom (viền + thông tin, Ubuntu Sans)
ML/data/*.CUB             cube Film LUT (README_FILMLUT.txt)
ML/SETTINGS/WINDYFLM.CFG  cấu hình (module tự ghi)
ML/LOGS/WINDYFLM.LOG      nhật ký, ghi nối tiếp qua các lần bật máy; mỗi lần
                          bật máy bắt đầu bằng dòng "WindyFilm <ngày>: camera ..."
ML/LOGS/WINDYFLM.OLD      nhật ký cũ (khi WINDYFLM.LOG quá 512 KB lúc bật máy)
MD5SUMS.txt               md5 của mọi file khi làm thẻ
