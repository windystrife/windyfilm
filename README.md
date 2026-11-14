# WindyFilm

Module Magic Lantern cho **Canon EOS M (firmware 2.0.2)** và **Canon 5D Mark II (2.1.2)**. Module tạo màu phim cho
ảnh JPEG ngay lúc chụp. Tác giả: **WindyStrife**. Giấy phép GPL-2.0, xem `LICENSE`; dữ liệu bên thứ ba ghi ở
`ATTRIBUTIONS.md`.

WindyFilm là module độc lập, thay cho bản "Film Roll port" cũ. Bản cũ là code của WindyStrife chạy bên trong module
`filmroll.mo` của tác giả khác. WindyFilm không dùng, không nạp và không chứa bất cứ thứ gì của module đó.
**Đừng bật hai module cùng lúc.**

> **Trạng thái 14/11/2026: đã có thẻ thử `F:\MagicLatern\EOSM_WindyFilm` và `F:\MagicLatern\5D2_WindyFilm`, nhưng
> module chưa chạy trên máy thật.**
>
> - Test host: EOS M 429 phép kiểm tra (có probe RAW P0 và đường cong Film 1), 5D2 383 phép, đều 0 lỗi.
> - Cổng kiểm tra nguồn gốc: PASS.
>
> Tình trạng mới nhất ở `docs/HANDOFF-QWEN.md`.

## Tính năng

- **Màu phim lúc chụp.** Mỗi ảnh JPEG đi qua bảng màu 17³ YCbCr của phim đang chọn (nhân 2.5D, xử lý theo từng khúc).
  - EOS M: hook ở bước Canon chuyển YUV (NN 5184x3456 hoặc Diana hai nửa).
  - 5D2: hook đường A, hai nửa 5616x1872.
- **Danh mục phim**, chia nhóm Colour / Cine / B&W / Film LUT. Mỗi phim có mô tả ngắn.
- **Hàng Light:**
  - Auto: giữ nguyên WB máy;
  - Day: WB theo phim;
  - Cloudy: WB phim x1,25;
  - Tungsten: 3200 K.
  WB của bạn được lưu lại và trả về khi chọn Auto hoặc tắt module.
- **Hàng Print:** Scan (scan trung tính), Cine print (in lên phim chiếu kiểu 2383), Paper (in giấy màu). Chỉ có với
  những phim có mô hình.
- **Hiệu ứng:**
  - Grain (kiểu Yedlin);
  - Halation và bloom;
  - Mist (chỉ EOS M);
  - Strength 30 / 100 / 125 %.
- **Đầu ra log:** S-Log3, V-Log, LogC3, Canon Log 2, Canon Log 3, dạng phẳng hoặc **+ LUT** (cube trên thẻ). Có LUT EI.
- **Input curve** (cho Log + LUT và các lon Film LUT): sRGB (mặc định) hoặc **Canon Film 1**.
  - Canon Film 1 đảo đúng đường cong Picture Style "Film 1" (User Def. 1) đo trên EOS M từ các cặp RAW+L của chủ dự
    án, thay cho sRGB. Xám 18 % ra đúng chỗ, và phần sáng trên trắng (tới 2,07) đi vào đoạn log của đường cong.
  - Chỉ đúng khi máy để Picture Style Film 1. Log phẳng và phim trong danh mục vẫn đọc sRGB.
- **Film LUT can:** chọn một lon là đặt luôn V-Log + LUT với cube của lon đó.
- **Xem trước trên LiveView** bằng cùng nhân màu, kèm xem trước độ phơi sáng.
- **EXIF:** UserComment ghi phim, kiểu in, Light, hiệu ứng và EV của từng ảnh.
- **Nhật ký:** `ML/LOGS/WINDYFLM.LOG`.
- **RAW probe (P0, chỉ EOS M, để thử):** menu Shoot > WindyFilm > RAW probe. Probe ghi vào log và
  `ML/LOGS/WFPROBE.BIN` địa chỉ RAW / SSRAW của Canon lúc chụp, không đổi ảnh, và tự tắt mỗi lần bật máy. Đây là bước
  đầu của đường RAW (`docs/DESIGN.md` 3.2).

## Giao diện chọn phim

Mở từ menu **WindyFilm > Browser**, hoặc bằng phím tắt: EOS M giữ lâu nút DOWN, 5D2 bấm TRASH.

- Bên trái là lon phim lớn, có quầng sáng theo màu của phim.
- Bên phải là tên phim, các chip (loại, ISO, khổ), mô tả ngắn và 7 hàng: Light, Print, Grain, Halation, Mist,
  Strength, Film LUT.
- Phía dưới là dải phim 35 mm cuộn ngang, có tab nhóm và thanh cuộn.

| Việc | EOS M | 5D2 |
|---|---|---|
| Đổi phim (sang được nhóm khác) | trái / phải | joystick trái / phải |
| Chọn hàng | lên / xuống | joystick lên / xuống |
| Đổi giá trị hàng đang chọn | vòng xoay sau | bánh xe trên hoặc bánh xe sau |
| Nạp phim | SET | SET hoặc nhấn joystick |
| Nhóm kế tiếp | chạm 1 ngón | Picture Style |
| Trang chi tiết | INFO | INFO |
| Đóng | MENU hoặc chạm 2 ngón | MENU hoặc TRASH |

- Bấm nửa hoặc hết nút chụp thì giao diện đóng và máy chụp như thường.
- EOS M cần đang ở LiveView chụp ảnh. Phím mở giao diện cần thẻ có core ML với patch 0002 / 0003 (thẻ EOS M hiện tại
  đã có).
- Bảng phím được chốt sau lần chạy thử đầu tiên trên máy.

## Cài lên thẻ

Thẻ WindyFilm nằm ở thư mục riêng (`EOSM_WindyFilm`, `5D2_WindyFilm`), không đụng vào các thẻ Film Roll đang dùng.
Thẻ được tạo bằng `tools/mk_card.sh` (PLAYBOOK mục 7).

```
ML/modules/windyflm.mo
ML/data/WFFILMS.DAT    bảng màu của các phim (Scan / Cine / Paper)
ML/data/WFCANS.DAT     hình lon phim
ML/data/WFFONTS.DAT    font chữ nhỏ của giao diện
ML/data/*.CUB          cube Film LUT
ML/SETTINGS/WINDYFLM.EN  bật module lúc khởi động
README_WINDYFILM.txt, README_FILMLUT.txt, CHANGELOG_FILMLUT.txt, SPEKTRAFILM_LICENSE.txt,
UBUNTU_FONT_LICENCE.txt, ATTRIBUTIONS.txt, LICENSE_WINDYFILM.txt, MD5SUMS.txt
```

- `autoexec.bin` và file `.sym` của máy (`EOSM_202.sym` / `5D2_212.sym`) phải luôn đi đúng cặp. Giữ nguyên cặp thẻ
  đang có.
- `filmroll.mo` không được có trên thẻ WindyFilm.

## Build và test (WSL Ubuntu-24.04)

```bash
bash build.sh CAM=EOSM
bash build.sh CAM=5D2
bash test.sh
```

Chạy từ một file script trong WSL; lệnh đầy đủ ở `docs/PLAYBOOK-QWEN.md`. `build.sh` kiểm tra các việc sau:
- mọi symbol module dùng đều có trong file `.sym` của thẻ;
- kích thước image khi TCC liên kết;
- cổng nguồn gốc (`tools/provenance_gate.py`): không có chuỗi hay đoạn byte nào trùng với module cũ.

Không chạy bộ test EOS M và 5D2 cùng lúc.

## Tài liệu

- `docs/DESIGN.md`: kiến trúc, định dạng các file trên thẻ, giao diện giữa các phần.
- `docs/PLAYBOOK-QWEN.md`, `docs/HANDOFF-QWEN.md`: hướng dẫn cho Qwen Code và tình trạng hiện tại.
- `ATTRIBUTIONS.md`: ghi công dữ liệu và thông số bên thứ ba.
