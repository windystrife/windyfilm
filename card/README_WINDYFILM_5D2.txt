WindyFilm cho Canon 5D Mark II (firmware 2.1.2) - thẻ thử đầu tiên, 14/11/2026
==============================================================================

Module Magic Lantern tạo màu phim cho ảnh JPEG ngay lúc chụp.
Tác giả: WindyStrife. Giấy phép GPL-2.0 (LICENSE_WINDYFILM.txt).
Dữ liệu và thông số bên thứ ba: ATTRIBUTIONS.txt.
Bảng Film stage (lon G00..G44 trong WFFILMS.DAT và WFSTAGE.DAT) là dữ liệu dẫn xuất
(DESIGN 1a): chỉ dùng riêng, không chia sẻ.

Thẻ RAWB24 (17/11 chiều, nhánh wf-onescreen): module dựng lại cùng mã với thẻ EOS M
RAWB24. Hai chỗ sửa là của Darkroom (5D2 không có). Trên 5D2 chỉ khác: màn hình sáng
lại thì browser lấy lại màu giao diện ngay. Thử như RAWB23.

Thẻ RAWB23 (17/11, nhánh wf-onescreen): browser có nút "Film | Film stage" ở góc trên
trái (Film: Colour / Cine / B&W / Film LUT; Film stage: Vision3 / Vision / ECN/EXR /
Fuji / B&W), chip chế độ ở góc trên phải (JPEG / RAW develop; 5D2 không có Darkroom
nên không có cuộn), INFO mở trang Info. Thêm Light Shade. Xem mục 2 và mục 3 bước 6.

Thẻ RAWB19 (16/11 tối, nhánh paper-finish, từ main-staging RAWB17): thêm hàng Paper
(giấy in: Glossy / Matte / Warm / Cool), xem mục 2 và bước 7b. Paper Glossy (mặc định)
cho ảnh y hệt RAWB17.

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
Grain, Halation và Bloom mặc định tắt trên 5D2 (mỗi nửa ảnh tốn vài giây).


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
- Hàng Light: Auto (giữ WB của bạn), Day (WB theo phim), Cloudy, Shade, Tungsten.
  WB của bạn được trả lại khi về Auto hoặc khi tắt module.
- (17/11) Nút "Film | Film stage" ở góc trên trái: bấm lên tới đó, SET (hoặc nhấn
  joystick) đổi section; TAP / PICT chỉ đổi tab trong section (Film: Colour / Cine /
  B&W / Film LUT; Film stage: Vision3 / Vision / ECN/EXR / Fuji / B&W).
- Hàng Print stock (trước gọi là Print): Scan / Cine print / Paper với phim có mô
  hình in; với lon Film stage là 15 print stock (mục 6).
- Hàng Grain: Off / Fine / Normal / Strong (độ hạt đổi theo độ sáng của vùng ảnh) và
  Even Fine / Even Normal / Even Strong (hạt như nhau ở mọi độ sáng). Mặc định Off
  trên 5D2.
- Hàng Halation / Bloom (từ 15/11, hai hàng riêng; mặc định Off + Off trên 5D2):
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
  áp cho Log phẳng. Cũng có trong menu ML. Trình chọn giờ có 23 hàng (11 hàng cuối
  chỉ dùng với lon Film stage, mục 6; hàng không dùng được thì mờ): hiện 7, danh
  sách tự cuộn theo hàng đang chọn (thanh cuộn nhỏ bên phải).
- Hàng Paper (16/11, ngay dưới Print stock): giấy của bản in, áp ở bước cuối cho mọi
  look có bản in: phim, lon Film stage, Log + LUT và lon Film LUT, RAW develop B. Log
  phẳng và Off thì hàng mờ.
    Glossy  giấy bóng, mặc định: ảnh như trước, không đổi một bit nào.
    Matte   giấy mờ: đen nâng lên (mã 0 thành khoảng 28), vùng tối bớt tương phản và
            bớt màu một chút, vùng sáng giữ nguyên.
    Warm    đế giấy màu kem (trắng thành 255 / 250 / 244) và tông nâu ấm ở vùng tối.
    Cool    đế giấy trắng xanh (247 / 249 / 255) và tông đen xanh ở vùng tối.
  Khác với Print stock "Paper" (mô hình in của phim): hai hàng dùng chung được. Đổi
  giấy chỉ dựng lại bảng màu một lần (phim khoảng 30 ms, Log + LUT hay RAW develop
  khoảng 55 ms, đo trên mô phỏng EOS M), không làm chậm lúc chụp. EXIF ghi "| Paper
  Matte" (không ghi khi Glossy); cấu hình: paper=glossy|matte|warm|cool. Cũng có trong
  menu ML.
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
   "hooks: installed (0)", "pack ok: 692 tables, index NNms, catalog 97 films" và
   "stage ok: 643 of 643 pairs match the pack (44 negatives, 15 prints), index NNms"
   (dòng "stage ok" là của Film stage, xem mục 7).
2. Chụp một ảnh mỗi chế độ: phim (Print Scan / Cine / Paper), Log, Log + LUT,
   một lon Film LUT. Mỗi nửa ảnh có dòng "yuv TOP ..." / "yuv BOT ..." trong log.
3. Light Day / Cloudy / Shade / Tungsten rồi về Auto: WB trở lại như cũ.
4. Các phím của trình chọn phim; màu giao diện khi LiveView ở phía sau.
5. EXIF (UserComment) của ảnh có dòng "WindyFilm: ...".
6. (17/11, RAWB23) Thanh trên cùng của browser: bấm lên từ hàng đầu tới đó, trái = nút
   "Film | Film stage", phải = chip chế độ, xuống = về các hàng.
   - SET (hoặc nhấn joystick) trên nút đổi section; section đang chọn chữ trắng. TAP /
     PICT chỉ đổi tab trong section, bộ đếm đếm trong section. Đổi đi rồi đổi lại: về
     đúng tab và phim lần trước.
   - Chip: JPEG / RAW develop (không có Darkroom, không có hàng Exposures). Ở Film
     stage chip ghi "JPEG 8-bit"; dưới tên phim có 2 dòng EXIF.
   - Chụp một ảnh lon Film stage ở JPEG 8-bit và một ảnh cùng phim ở RAW develop: EXIF
     có thêm "| Mode ... | Dev n.n s", log có "dev time: ... N ms".
   - INFO mở trang Info (chữ "Info", tên phim, góc phải "FILM STAGE" hay "FILM"; dưới
     đó các chip loại phim / ISO / 35mm và 2 dòng mô tả phim; trong bảng có Table và
     Daylight; dưới cùng là bảng phím với tên phím của 5D2: DIAL, JOY, PICT, MENU hay
     TRASH); INFO lần nữa đóng lại. Dòng "Last develop" ghi thời gian và chế độ của ảnh vừa chụp:
     ghi lại số của JPEG 8-bit và của RAW develop để so.
   - Light Auto: WB trong menu Canon không bị đổi (chụp vài ảnh rồi mở menu xem).


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
- Bảng RAW develop của lon Log + LUT / Leica (mới): lần đầu dựng từ cube (cube
  65^3 của CinePrint lâu hơn, có thể vài chục giây), rồi lưu ở ML/DATA/<tên
  cube>.RBT (356 KB). Những lần sau (bật máy, đổi phim, xem ảnh xong) đọc file
  đó, khoảng 1 giây. Log ghi "... .RBT hit" hoặc lý do dựng lại. Xoá file .RBT
  thì lần sau dựng lại, không sao.

Gửi lại: ML/LOGS/WINDYFLM.LOG và các cặp CR2 + JPG (kèm 1 tấm cùng cảnh với
RAW develop Off). Log có các dòng "rawb: ..." cho từng nửa ảnh: địa chỉ RAW,
điểm khớp, mức đen / trắng, thời gian từng bước.


6. FILM STAGE: ÂM BẢN x PRINT STOCK (15/11)
-------------------------------------------
Vào: trong trình chọn phim, bấm Picture Style để qua nhóm kế tiếp: Colour -> Cine ->
B&W -> Film LUT -> Film stage (nhóm cuối, biểu tượng lon tím). Nhóm có 44 lon, mã
G00 .. G44 (không có G33), vẽ theo màu hãng: Kodak Vision3 / Vision2 / Vision / EXR /
ECN / Double-X / Primetime, Fujifilm Eterna / Eterna Vivid / Reala / Super F / Color A.

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
- Double-X 5222 (G09) là âm bản đen trắng: chỉ in lên 2302 (hàng Print stock chỉ có
  2302 và đứng sẵn ở đó), nhãn B&W NEG, ảnh đen trắng, hạt và halation kiểu phim đen
  trắng. 43 lon còn lại là âm bản màu.
- Không có Fuji Eterna-CP Type 3513D: chuỗi không có bảng của print này.
- Năm cặp cho ảnh giống hệt nhau trên mọi âm bản (cùng một bảng): 2383 = 2393,
  5380 = 5381/72, 2395 = 5384/82, 5384/92 = 5385/93, 5385/62 = 5386. Cả hai tên được
  giữ như danh sách gốc; thực tế có 10 kiểu in khác nhau.
- ECN-I 5248 25T (G17) chỉ có 12 print (thiếu 2302, 5383, 3523XD).
- Trình chọn hiện tên đầy đủ khi vừa chỗ; menu ML hiện mã, tên đầy đủ ở dòng help.
  EXIF ghi tên đầy đủ ("| Print Kodak Vision Color 2383").
- Grain, Halation, Bloom, Gauge, Mist, Strength, Highlights / Shadows dùng như phim
  thường (mục 2).

11 hàng cuối (hàng knob) chỉnh chính chuỗi âm bản -> print. Chỉ dùng được khi: lon
Film stage, Output Film, và có ML/DATA/WFSTAGE.DAT trên thẻ; không đủ thì hàng mờ.
Sửa bằng bánh xe, hoặc nhấn joystick ("JOY Edit") rồi trái / phải.
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
    Interlayer ..... 0 .. 200 %, bước 25 %, mặc định 50 % (từ 16/11 khuya; 100 % =
                     của chuỗi, bảng gốc). Hiệu ứng giữa các lớp màu: thấp = màu nhạt
                     và lẫn hơn, cao = màu tách và đậm hơn. Thẻ đã lưu cài đặt
                     (dòng stage= trong ML/SETTINGS/WINDYFLM.CFG) thì giữ giá trị cũ:
                     muốn 50 % thì chỉnh hàng này một lần.
  Trong trình chọn phim, các hàng sáng / mờ theo phim đang đứng (lướt trái / phải),
  không cần bấm SET: lon Film stage thì hàng knob sáng, phim thường thì mờ, lon Film
  LUT thì Print stock và Strength mờ. Chỉnh hàng trước khi SET cũng được: giá trị
  dùng chung cho mọi phim.
    (16/11, 4 hàng mới, cả bốn 0 .. 100 %, bước 25 %, mặc định 0 % = tắt)
    Bleach bypass .. bỏ tẩy bạc ở âm bản: bạc giữ lại cộng vào cả ba lớp màu, ảnh
                     nhạt màu và tương phản khác đi.
    Print bleach ... bỏ tẩy bạc ở bản in: chỗ in đậm tối thêm, ảnh tối và nhạt màu
                     hơn (kiểu bleach bypass trên print).
                     Hai hàng bleach: 100 % = 1/16 tham số của chuỗi. Chuỗi cho tới 1,0
                     nhưng ở 0,25 xám giữa đã gần trắng (Bleach bypass) hoặc gần đen
                     (Print bleach). Ở 100 % trên 2 lon x 16 ảnh: Bleach bypass sáng hơn
                     rõ (L* 55 -> 68) và bớt màu 18 %, Print bleach tối hơn (L* 55 -> 43)
                     và bớt màu 22 %. Muốn giữ độ sáng thì bù bằng Film exp / Printer.
    Neutral neg .... đường cong R và B của âm bản kéo về đường cong G: ảnh trung tính
                     hơn, bớt lệch màu theo độ sáng.
    Neutral print .. như trên, cho đường cong của print.
  4 hàng này cần WFSTAGE.DAT của thẻ 16/11 trở đi (bản 2, có thêm 520 B dữ liệu knob);
  WFSTAGE.DAT của thẻ cũ vẫn chạy 7 hàng kia, 4 hàng mới mờ.
- Để mặc định hết (0 / 0 / Off / 0 0 0 / 100 % / 0 0 0 0) thì máy dùng bảng có sẵn
  trong WFFILMS.DAT như mọi phim, không đọc WFSTAGE.DAT.
- Đổi một hàng knob (hoặc đổi phim / print khi knob khác mặc định): máy dựng bảng mới
  từ WFSTAGE.DAT. Không đọc cả file 21,3 MB: lúc bật máy chỉ đọc mục lục (khoảng
  20 KB); mỗi lần dựng đọc khoảng 650 KB của đúng cặp đó (nửa âm bản 435 KB, rồi
  nửa print 217 KB, cần khoảng 470 KB bộ nhớ tạm, trả lại ngay), tính toán khoảng
  40 ms trên 5D2. Log: "stage G08 v4 composed NNms k..." (bảng dựng từ knob) hoặc
  "pack G08 v4 loaded NNms" (bảng có sẵn). G08 = mã lon, v4 = print (v3 = 2302, v4 =
  2383 .. v18 = 3523XD).
- Cấu hình ghi dòng "stage=fexp,push,comp,r,g,b,iie,bleach,printbleach,neutralneg,
  neutralprint" (11 số; dòng 7 số của thẻ cũ vẫn đọc được, 4 hàng mới = 0). EXIF thêm
  "| Film exp +1", "| Push 1/3", "| Exp comp", "| Printer R+6 G0 B0", "| Interlayer 50%",
  "| Bleach bypass 50%", "| Print bleach 25%", "| Neutral neg 100%", "| Neutral print 50%"
  khi khác mặc định.
- 5D2 không có Darkroom (cuộn phim chỉ có trên EOS M).


7. FILM STAGE: LẦN CHẠY ĐẦU
---------------------------
1. Thẻ phải có ML/DATA/WFSTAGE.DAT (21 324 388 B từ thẻ 16/11; md5 trong MD5SUMS.txt). Bật máy,
   WINDYFLM.LOG phải có:
     pack ok: 692 tables, index NNms, catalog 97 films
     stage ok: 643 of 643 pairs match the pack (44 negatives, 15 prints), index NNms
   Ghi lại hai số "index NNms" (thời gian đọc mục lục; thẻ CF). Nếu thay vào đó là
   "stage ML/DATA/WFSTAGE.DAT missing (-1): film-stage knob rows hidden" thì thẻ thiếu
   file; "... bad (-N) ..." là file hỏng hoặc khác bản: chép lại và kiểm MD5SUMS.txt.
   Thiếu file thì lon Film stage vẫn chụp được, chỉ mất 11 hàng knob.
1b. (16/11 khuya) Interlayer mặc định 50 %: chụp vài tấm như thường (log có
   "stage G08 v4 composed NNms k...", EXIF "| Interlayer 50%"). Trong trình chọn, lướt
   từ lon Film stage sang phim thường và lon Film LUT, chưa bấm SET: hàng knob phải
   sáng / mờ ngay theo phim đang đứng. Các bước dưới viết cho bảng gốc: đặt
   Interlayer 100 % trước bước 2.
2. Nhóm Film stage: lon có màu hãng, header đúng phim (ví dụ G08 Vision3 5219 500T:
   ISO 500, 3200 K). Nạp G08, để Print stock mặc định: hàng hiện "Kodak Vision Color
   2383". Chụp 1 tấm: ảnh màu. Log "pack G08 v4 loaded NNms", rồi hai dòng
   "yuv TOP ..." / "yuv BOT ...".
3. Print stock 2302: chụp, ảnh ĐEN TRẮNG, log "pack G08 v3 loaded". Rồi 2393, cùng cảnh
   với tấm ở bước 2: ảnh phải giống hệt tấm 2383 (cặp trùng). Trong hàng Print stock
   không có 3513D. Nạp G09 Double-X 5222: hàng Print stock chỉ có 2302, ảnh đen trắng,
   log "pack G09 v3 loaded". Nạp lại G08 cho các bước sau.
4. Print stock về 2383, Film exp +1, chụp. Log
   "stage G08 v4 composed NNms k...": ghi lại NN (dự tính
   khoảng 40 ms tính + thời gian đọc khoảng 650 KB từ thẻ). Ảnh sáng hơn tấm ở bước 2,
   nửa trên và nửa dưới cùng màu (không có vạch ngang ở giữa). EXIF có "| Film exp +1".
   Chụp thêm 1 tấm ngay sau: không dựng lại (không có dòng composed mới).
5. Film exp về 0, Printer R +6: ảnh ngả xanh lơ so với bước 2 (ghi lại nếu ngả đỏ).
   EXIF "| Printer R+6 G0 B0". Printer R về 0, Printer B +6: ngả vàng.
6. Film exp +1, Exp comp On: Film exp và Push / pull mờ đi, ảnh như bước 2; EXIF có
   "| Exp comp".
7. Exp comp Off, Film exp 0, Interlayer 0 % rồi 200 %: màu nhạt rồi đậm hơn.
7b. (16/11) Interlayer về 100 %. 4 hàng mới phải sáng (không mờ). Mỗi hàng chụp 1 tấm
   cùng cảnh với bước 2, rồi đưa hàng đó về 0 % trước hàng sau:
   - Bleach bypass 100 %: sáng hơn và nhạt màu hơn tấm bước 2 (đo trên máy tính:
     L* 55 -> 68, bớt màu 18 %). EXIF "| Bleach bypass 100%".
   - Print bleach 100 %: tối hơn và nhạt màu hơn tấm bước 2. EXIF "| Print bleach 100%".
   - Neutral neg 100 %, rồi Neutral print 100 %: màu trung tính hơn, đổi ít (có lon gần
     như không đổi).
   Mỗi lần đổi có một dòng "stage G08 v4 composed NNms k..." mới: ghi lại NN.
8. Grain Normal, Halation Wide, Gauge 16 mm (mỗi nửa ảnh tốn thêm vài giây): hạt to
   hơn, quầng rộng hơn so với 35 mm; 65 mm: mịn hơn, quầng hẹp hơn. EXIF "| Gauge 16 mm".
9. Bộ nhớ: lặp bước 4 với LiveView đang bật, và một lần ngắm qua kính. Nếu log có
   "stage: no memory for a half (N B) in 3000ms: film-stage knobs dropped" thì máy bỏ
   các hàng knob và dùng bảng mặc định: ghi lại lúc đó đang làm gì.
10. Đổi nhanh nhiều hàng knob rồi chụp ngay: ghi lại tấm đầu có phải chờ không.
11. Tắt máy bật lại: 11 hàng knob giữ giá trị đã đặt.
Gửi: ML/LOGS/WINDYFLM.LOG, ML/SETTINGS/WINDYFLM.CFG và các JPG (kèm CR2 nếu chụp RAW+L).


7b. HÀNG PAPER: CẦN THỬ (16/11)
-------------------------------
1. Phim thường (ví dụ Portra 400), cảnh có vùng đen sâu và tường / trời trắng, chân
   máy: 4 tấm cùng cảnh, cùng phơi sáng, Paper Glossy / Matte / Warm / Cool. Matte:
   đen thành xám than nhẹ, trắng giữ nguyên; Warm: trắng ngả kem; Cool: trắng ngả
   xanh nhẹ. Log: "shot #n ... paper Matte"; EXIF "| Paper Matte".
2. Một phim đen trắng (HP5 Plus): Warm thành nâu ấm, Cool thành xám xanh (rõ hơn
   ảnh màu). Ghi lại nếu Warm / Cool thấy quá đậm hay quá nhạt.
3. Log + LUT (một lon Windy ... hoặc Leica Vivid), Paper Matte: log "lut ... paper
   Matte". Đổi Paper không đọc lại cube từ đầu (cache .FRT).
4. Một lon Film stage (ví dụ G08 + 2383), Paper Matte rồi Warm.
5. RAW develop On (RAW+L) với một lon Windy ..., Paper Warm: log "rawb: paper Warm
   NNms": ghi lại số ms (mô phỏng EOS M: khoảng 55 ms mỗi lần dựng bảng).
6. Log phẳng hoặc Off: hàng Paper mờ, không đổi được.
7. Tắt máy bật lại: Paper giữ giá trị (WINDYFLM.CFG có dòng paper=...).
Gửi: ML/LOGS/WINDYFLM.LOG, ML/SETTINGS/WINDYFLM.CFG và các JPG (kèm CR2 nếu chụp RAW+L).


8. FILE TRÊN THẺ
----------------
ML/modules/windyflm.mo    module
ML/data/WFFILMS.DAT       bảng màu các phim (Scan / Cine / Paper, và 643 cặp Film stage)
ML/data/WFSTAGE.DAT       hai nửa âm bản / print của 643 cặp Film stage (21,3 MB),
                          cho 11 hàng knob (mục 6); thiếu thì các hàng đó mờ.
                          Dữ liệu dẫn xuất, chỉ dùng riêng
ML/data/WFCANS.DAT        hình lon phim
ML/data/WFFONTS.DAT       font chữ của giao diện (Ubuntu Sans, UBUNTU_FONT_LICENCE.txt)
ML/data/*.CUB             cube Film LUT (README_FILMLUT.txt)
ML/SETTINGS/WINDYFLM.CFG  cấu hình (module tự ghi)
ML/LOGS/WINDYFLM.LOG      nhật ký, ghi nối tiếp qua các lần bật máy; mỗi lần
                          bật máy bắt đầu bằng dòng "WindyFilm <ngày>: camera ..."
ML/LOGS/WINDYFLM.OLD      nhật ký cũ (khi WINDYFLM.LOG quá 512 KB lúc bật máy)
ML/LOGS/WFPROBE.BIN       bản ghi của RAW probe (chỉ khi bật probe)
MD5SUMS.txt               md5 của mọi file khi làm thẻ
