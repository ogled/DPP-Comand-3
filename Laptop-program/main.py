import sys
import json
import struct
import threading
from datetime import datetime

import serial
import serial.tools.list_ports

from PyQt6.QtCore import QObject, Qt, pyqtSignal
from PyQt6.QtGui import QAction, QFont
from PyQt6.QtWidgets import (
    QApplication,
    QComboBox,
    QFileDialog,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QListWidget,
    QListWidgetItem,
    QMainWindow,
    QPushButton,
    QSplitter,
    QTextEdit,
    QVBoxLayout,
    QWidget,
)

MARK_LOG = 0x01
MARK_DATA = 0x02
HEADER_SIZE = 12
MAX_PACKET_SIZE = 64
MAX_PAYLOAD_SIZE = MAX_PACKET_SIZE - HEADER_SIZE

TYPE_NAMES = {0: "I2C", 1: "SPI", 2: "UART", 3: "Digital", 4: "Analog"}
UART_NAMES = {0: "Serial", 1: "Serial1", 2: "Serial2", 3: "Serial3"}


class PacketParser:
    def __init__(self, callback):
        self.buffer = bytearray()
        self.callback = callback

    def feed(self, data):
        self.buffer.extend(data)
        while self.buffer:
            marker = self.buffer[0]

            if marker == MARK_LOG:
                end = self.buffer.find(b"\n", 1)
                if end < 0:
                    return
                self.callback("log", bytes(self.buffer[1:end]))
                del self.buffer[:end + 1]
                continue

            if marker == MARK_DATA:
                if len(self.buffer) < 3:
                    return
                length = self.buffer[1] | (self.buffer[2] << 8)
                if length > MAX_PACKET_SIZE:
                    del self.buffer[0]
                    continue
                total = 3 + length
                if len(self.buffer) < total:
                    return
                self.callback("data", bytes(self.buffer[3:total]))
                del self.buffer[:total]
                continue

            positions = [
                p for p in (
                    self.buffer.find(bytes([MARK_LOG])),
                    self.buffer.find(bytes([MARK_DATA]))
                ) if p >= 0
            ]
            if not positions:
                self.buffer.clear()
                return
            del self.buffer[:min(positions)]


class Receiver(QObject):
    frame = pyqtSignal(str, object)

    def __init__(self, ser):
        super().__init__()
        self.ser = ser
        self.stop_event = threading.Event()
        self.parser = PacketParser(self.handle_frame)

    def handle_frame(self, kind, data):
        self.frame.emit(kind, data)

    def run(self):
        while not self.stop_event.is_set():
            try:
                if not self.ser or not self.ser.is_open:
                    break
                count = self.ser.in_waiting
                if count:
                    data = self.ser.read(count)
                    if data:
                        self.parser.feed(data)
                else:
                    self.stop_event.wait(0.01)
            except (serial.SerialException, OSError) as exc:
                if not self.stop_event.is_set():
                    self.frame.emit("error", str(exc))
                break


class Messenger(QMainWindow):
    def __init__(self):
        super().__init__()
        self.ser = None
        self.receiver = None
        self.thread = None
        self.write_lock = threading.Lock()
        self.next_id = 1
        self.history = []
        self.records = {}
        self.paused = False
        self.selected_id = None
        self.rx_count = 0
        self.tx_count = 0
        self.rx_bytes = 0
        self.tx_bytes = 0
        self.error_count = 0

        self.setWindowTitle("DPP · Монитор последовательного порта")
        self.setMinimumSize(1080, 700)
        self.resize(1440, 880)
        self.setStyleSheet("""
            QMainWindow, QWidget { background:#11151d; color:#e6eaf2; font-size:13px; }
            QMenuBar { background:#151b25; color:#c8d0df; padding:5px 10px; }
            QMenuBar::item:selected, QMenu::item:selected { background:#26354a; }
            QMenu { background:#1a2230; border:1px solid #303c50; }
            QLineEdit, QComboBox {
                background:#171e29; border:1px solid #303b4d; border-radius:8px;
                padding:9px 11px; color:#e6eaf2; selection-background-color:#376ba6;
            }
            QLineEdit:focus, QComboBox:focus { border:1px solid #5b9be5; }
            QComboBox QAbstractItemView {
                background:#171e29; color:#e6eaf2; selection-background-color:#263d5b;
                border:1px solid #303b4d;
            }
            QPushButton {
                background:#202a39; border:1px solid #344258; border-radius:8px;
                padding:9px 14px; color:#e6eaf2; font-weight:600;
            }
            QPushButton:hover { background:#2a394d; border-color:#4a6383; }
            QPushButton:pressed { background:#172131; }
            QPushButton:disabled { color:#6e7888; background:#171e29; border-color:#273142; }
            QPushButton#primary { background:#2465a8; border-color:#347bc4; color:white; }
            QPushButton#primary:hover { background:#2d78c3; }
            QPushButton#alarm { background:#a83a42; border-color:#c44d55; color:white; }
            QPushButton#alarm:hover { background:#c34952; }
            QListWidget {
                background:#121822; border:1px solid #242e3e; border-radius:10px;
                outline:none; padding:8px;
            }
            QListWidget::item {
                background:#19212d; border:1px solid #263244; border-radius:8px;
                padding:10px; margin:4px 2px;
            }
            QListWidget::item:hover { background:#202c3c; border-color:#3a4b64; }
            QListWidget::item:selected { background:#203653; border:1px solid #4b83bb; }
            QTextEdit {
                background:#121822; border:1px solid #263244; border-radius:10px;
                padding:12px; color:#dce4ef; selection-background-color:#376ba6;
            }
            QSplitter::handle { background:#263244; width:2px; }
            QLabel#brand { color:#f2f5fa; font-size:20px; font-weight:700; }
            QLabel#subtitle, QLabel#muted { color:#8793a5; }
            QLabel#title { color:#f2f5fa; font-size:16px; font-weight:700; }
            QLabel#status {
                background:#252c38; border:1px solid #343f50; border-radius:9px;
                padding:8px 12px; color:#b6c0ce; font-weight:700;
            }
            QLabel#status[connected="true"] {
                background:#17362c; border-color:#28644c; color:#7ed6aa;
            }
            QLabel#stat { background:#171e29; border:1px solid #263244; border-radius:7px; padding:6px 9px; }
        """)
        self.build_ui()
        self.build_menu()
        self.update_ports()
        self.update_status()

    def build_ui(self):
        brand = QVBoxLayout()
        brand.setSpacing(2)
        title = QLabel("DPP  /  UART MONITOR")
        title.setObjectName("brand")
        subtitle = QLabel("Монитор последовательного порта и радиопакетов")
        subtitle.setObjectName("subtitle")
        brand.addWidget(title)
        brand.addWidget(subtitle)

        header = QHBoxLayout()
        header.setContentsMargins(18, 14, 18, 12)
        header.addLayout(brand)
        header.addStretch()
        self.status = QLabel("● Отключено")
        self.status.setObjectName("status")
        header.addWidget(self.status)

        top = QHBoxLayout()
        top.setContentsMargins(18, 0, 18, 12)
        top.setSpacing(9)

        self.port = QComboBox()
        self.port.setMinimumWidth(280)
        self.port.setMinimumContentsLength(22)
        self.baud = QComboBox()
        self.baud.addItems(["9600"])
        self.baud.setCurrentText("9600")
        self.baud.setMinimumWidth(100)
        self.refresh_button = QPushButton("Обновить")
        self.connect_button = QPushButton("Подключить")
        self.connect_button.setObjectName("primary")
        self.pause_button = QPushButton("Пауза")
        self.pause_button.setEnabled(False)
        self.clear_button = QPushButton("Очистить")
        self.export_button = QPushButton("Экспорт")

        for widget in (self.port, self.baud, self.refresh_button, self.connect_button,
                       self.pause_button, self.clear_button, self.export_button):
            top.addWidget(widget)

        filters = QHBoxLayout()
        filters.setContentsMargins(18, 0, 18, 10)
        filters.setSpacing(9)
        self.search = QLineEdit()
        self.search.setPlaceholderText("Поиск по ID, Device ID, типу, сообщению...")
        self.search.setClearButtonEnabled(True)
        self.type_filter = QComboBox()
        self.type_filter.addItem("Все типы", -1)
        for value, name in TYPE_NAMES.items():
            self.type_filter.addItem(name, value)
        filters.addWidget(self.search, 1)
        filters.addWidget(self.type_filter)

        self.list = QListWidget()
        self.list.setWordWrap(True)
        self.list.setSpacing(3)
        self.list.setMinimumWidth(420)

        details = QWidget()
        details_layout = QVBoxLayout(details)
        details_layout.setContentsMargins(12, 12, 12, 12)
        details_layout.setSpacing(10)

        self.details_title = QLabel("Пакет не выбран")
        self.details_title.setObjectName("title")
        self.details = QTextEdit()
        self.details.setReadOnly(True)
        self.details.setFont(QFont("DejaVu Sans Mono", 10))
        self.copy_hex = QPushButton("Копировать HEX")
        self.copy_frame = QPushButton("Копировать кадр")
        self.copy_hex.setEnabled(False)
        self.copy_frame.setEnabled(False)

        buttons = QHBoxLayout()
        buttons.addWidget(self.copy_hex)
        buttons.addWidget(self.copy_frame)
        details_layout.addWidget(self.details_title)
        details_layout.addWidget(self.details, 1)
        details_layout.addLayout(buttons)

        splitter = QSplitter(Qt.Orientation.Horizontal)
        splitter.addWidget(self.list)
        splitter.addWidget(details)
        splitter.setHandleWidth(2)
        splitter.setCollapsible(0, False)
        splitter.setCollapsible(1, False)
        splitter.setStretchFactor(0, 6)
        splitter.setStretchFactor(1, 4)
        splitter.setSizes([850, 540])

        stats = QHBoxLayout()
        stats.setContentsMargins(18, 12, 18, 4)
        stats.setSpacing(8)
        self.rx_label = QLabel()
        self.tx_label = QLabel()
        self.bytes_label = QLabel()
        self.errors_label = QLabel()
        self.pause_label = QLabel("Лента активна")
        self.pause_label.setObjectName("muted")
        for label in (self.rx_label, self.tx_label, self.bytes_label, self.errors_label):
            label.setObjectName("stat")
            stats.addWidget(label)
        stats.addStretch()
        stats.addWidget(self.pause_label)

        input_layout = QHBoxLayout()
        input_layout.setContentsMargins(18, 10, 18, 16)
        input_layout.setSpacing(9)
        self.input = QLineEdit()
        self.input.setPlaceholderText("Введите сообщение (до 52 байт UTF-8)...")
        self.send_button = QPushButton("Отправить")
        self.alarm_button = QPushButton("ТРЕВОГА")
        self.alarm_button.setObjectName("alarm")
        input_layout.addWidget(self.input, 1)
        input_layout.addWidget(self.send_button)
        input_layout.addWidget(self.alarm_button)

        root = QWidget()
        layout = QVBoxLayout(root)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)
        layout.addLayout(header)
        layout.addLayout(top)
        layout.addLayout(filters)
        layout.addWidget(splitter, 1)
        layout.addLayout(stats)
        layout.addLayout(input_layout)
        self.setCentralWidget(root)

        self.refresh_button.clicked.connect(self.update_ports)
        self.connect_button.clicked.connect(self.toggle_connection)
        self.pause_button.clicked.connect(self.toggle_pause)
        self.clear_button.clicked.connect(self.clear_history)
        self.export_button.clicked.connect(self.export_history)
        self.send_button.clicked.connect(self.send)
        self.alarm_button.clicked.connect(self.send_alarm)
        self.input.returnPressed.connect(self.send)
        self.search.textChanged.connect(self.apply_filters)
        self.type_filter.currentIndexChanged.connect(self.apply_filters)
        self.list.currentItemChanged.connect(self.on_current_item_changed)
        self.copy_hex.clicked.connect(self.copy_selected_hex)
        self.copy_frame.clicked.connect(self.copy_selected_frame)

    def build_menu(self):
        file_menu = self.menuBar().addMenu("Файл")
        export_action = QAction("Экспорт JSON", self)
        clear_action = QAction("Очистить историю", self)
        exit_action = QAction("Выход", self)
        export_action.triggered.connect(self.export_history)
        clear_action.triggered.connect(self.clear_history)
        exit_action.triggered.connect(self.close)
        file_menu.addAction(export_action)
        file_menu.addAction(clear_action)
        file_menu.addSeparator()
        file_menu.addAction(exit_action)

    def update_ports(self):
        selected = self.port.currentData()
        self.port.clear()
        try:
            ports = serial.tools.list_ports.comports()
        except (serial.SerialException, OSError) as exc:
            self.add_system(f"Не удалось получить список портов: {exc}", True)
            return
        for item in ports:
            label = item.device
            if item.description and item.description.lower() != "n/a":
                label += f" — {item.description}"
            self.port.addItem(label, item.device)
        if not ports:
            self.port.addItem("Последовательный порт не найден", None)
        if selected:
            index = self.port.findData(selected)
            if index >= 0:
                self.port.setCurrentIndex(index)

    def toggle_connection(self):
        if self.ser and self.ser.is_open:
            self.disconnect()
        else:
            self.connect()

    def connect(self):
        device = self.port.currentData()
        if not device:
            self.add_system("COM-порт не выбран", True)
            return
        try:
            self.ser = serial.Serial(
                port=device,
                baudrate=int(self.baud.currentText()),
                timeout=0.05,
                write_timeout=1
            )
        except (serial.SerialException, OSError, ValueError) as exc:
            self.ser = None
            self.add_system(f"Ошибка подключения: {exc}", True)
            self.update_status()
            return

        self.receiver = Receiver(self.ser)
        self.receiver.frame.connect(self.receive_frame)
        self.thread = threading.Thread(target=self.receiver.run, daemon=True)
        self.thread.start()
        self.connect_button.setText("Отключить")
        self.pause_button.setEnabled(True)
        self.port.setEnabled(False)
        self.baud.setEnabled(False)
        self.refresh_button.setEnabled(False)
        self.add_system(f"Подключено: {device} @ {self.baud.currentText()}")
        self.update_status()

    def disconnect(self, log=True):
        was_connected = self.ser is not None or self.receiver is not None
        if self.receiver:
            self.receiver.stop_event.set()
        if self.ser:
            try:
                self.ser.close()
            except (serial.SerialException, OSError) as exc:
                self.error_count += 1
                self.add_system(f"Ошибка закрытия порта: {exc}", True)
        if self.thread and self.thread.is_alive():
            self.thread.join(timeout=1)
            if self.thread.is_alive():
                self.error_count += 1
                self.add_system("Поток чтения порта не завершился вовремя", True)
        self.receiver = None
        self.thread = None
        self.ser = None
        self.connect_button.setText("Подключить")
        self.pause_button.setEnabled(False)
        self.port.setEnabled(True)
        self.baud.setEnabled(True)
        self.refresh_button.setEnabled(True)
        if was_connected and log:
            self.add_system("Соединение закрыто")
        self.update_status()

    def send_bytes(self, data):
        if not self.ser or not self.ser.is_open:
            self.add_system("Нет подключения", True)
            return False
        try:
            with self.write_lock:
                written = self.ser.write(data)
            if written != len(data):
                raise serial.SerialTimeoutException(
                    f"Отправлено {written} из {len(data)} байт"
                )
            self.tx_count += 1
            self.tx_bytes += len(data)
            self.update_status()
            return True
        except (serial.SerialException, OSError) as exc:
            self.error_count += 1
            self.add_system(f"Ошибка отправки: {exc}", True)
            self.update_status()
            return False

    def send(self):
        text = self.input.text()
        if not text.strip():
            return
        payload = text.encode("utf-8")
        if len(payload) > MAX_PAYLOAD_SIZE:
            self.add_system(
                f"Сообщение слишком длинное: {len(payload)} байт, максимум {MAX_PAYLOAD_SIZE} байт",
                True
            )
            self.error_count += 1
            self.update_status()
            return

        data = payload + b"\n"
        if self.send_bytes(data):
            self.add_record({
                "id": self.new_id(),
                "kind": "TX",
                "time": self.now(),
                "title": "↑ Отправлено",
                "preview": text,
                "meta": f"UART → Serial · {len(data)} байт",
                "raw": data,
                "payload": data.rstrip(b"\n")
            })
            self.input.clear()

    def send_alarm(self):
        data = b"ALERT\n"
        if self.send_bytes(data):
            self.add_record({
                "id": self.new_id(),
                "kind": "TX",
                "time": self.now(),
                "title": "↑ ⚠ ТРЕВОГА",
                "preview": "ALERT",
                "meta": "UART → Serial · ALERT",
                "raw": data,
                "payload": b"ALERT"
            })

    def receive_frame(self, kind, data):
        if kind == "log":
            self.add_system(data.decode("utf-8", errors="replace"))
            return
        if kind == "error":
            self.error_count += 1
            self.add_system(str(data), True)
            self.disconnect(log=False)
            self.update_status()
            return
        if kind == "data":
            self.rx_count += 1
            self.rx_bytes += len(data) + 3
            self.handle_radio_packet(bytes(data))
            self.update_status()

    def handle_radio_packet(self, data):
        record = self.parse_packet(data)
        message_id = self.new_id()

        if record is None:
            record = {
                "id": message_id,
                "kind": "RX",
                "valid": False,
                "time": self.now(),
                "title": "↓ Некорректный пакет",
                "preview": data.hex(" "),
                "meta": f"RX · {len(data)} байт",
                "raw": data,
                "payload": data
            }
            self.error_count += 1
        else:
            record.update({
                "id": message_id,
                "kind": "RX",
                "valid": True,
                "time": self.now(),
                "raw": data
            })
            record.update({
                "title": f"↓ DEV-{record['device_id']:03d} · MSG-{message_id:06d}",
                "preview": self.make_preview(record),
                "meta": self.make_meta(record)
            })

        self.add_record(record)

    def parse_packet(self, data):
        if len(data) < HEADER_SIZE or len(data) > MAX_PACKET_SIZE:
            return None
        device_id = data[0]
        longitude = struct.unpack_from("<i", data, 1)[0]
        latitude = struct.unpack_from("<i", data, 5)[0]
        type_value = data[9]
        packed = data[10]
        type_input = packed & 0x0F
        packet_number = (packed >> 4) & 0x0F
        size_data = data[11]
        available = len(data) - HEADER_SIZE
        if size_data > available:
            return None
        return {
            "device_id": device_id,
            "longitude": longitude,
            "latitude": latitude,
            "type": type_value,
            "type_input": type_input,
            "packet_number": packet_number,
            "size_data": size_data,
            "payload": data[HEADER_SIZE:HEADER_SIZE + size_data],
            "trailing": data[HEADER_SIZE + size_data:]
        }

    def type_name(self, record):
        name = TYPE_NAMES.get(record["type"], f"Unknown ({record['type']})")
        if record["type"] == 2:
            uart_name = UART_NAMES.get(record["type_input"], f"Input {record['type_input']}")
            name += f" / {uart_name}"
        return name

    def decode_payload(self, record):
        payload = record["payload"]
        value_type = record["type"]
        input_type = record["type_input"]

        if value_type == 2 and input_type == 0:
            return payload.decode("utf-8", errors="replace")
        if value_type == 3:
            return str(payload[0]) if len(payload) == 1 else payload.hex(" ")
        if value_type == 4:
            if len(payload) == 1:
                return str(payload[0])
            if len(payload) == 2:
                return str(struct.unpack_from("<H", payload)[0])
            if len(payload) == 4:
                return f"uint32={struct.unpack_from('<I', payload)[0]}\nfloat={struct.unpack_from('<f', payload)[0]:.6g}"
        return payload.hex(" ")

    def make_preview(self, record):
        text = self.decode_payload(record).replace("\n", " ↵ ")
        if record["type"] == 2 and record["type_input"] == 0 and text.strip() == "ALERT":
            return "⚠ ТРЕВОГА"
        return text or "[пустые данные]"

    def make_meta(self, record):
        return (
            f"RX · {self.type_name(record)} · "
            f"PKT {record['packet_number']} · "
            f"{record['size_data']} байт · "
            f"{record['time']}"
        )

    def add_system(self, text, error=False):
        record = {
            "id": self.new_id(),
            "kind": "ERROR" if error else "SYS",
            "time": self.now(),
            "title": "Ошибка" if error else "Система",
            "preview": text,
            "meta": self.now(),
            "raw": text.encode("utf-8", errors="replace"),
            "payload": text.encode("utf-8", errors="replace"),
            "system": True,
            "valid": not error
        }
        self.add_record(record)

    def add_record(self, record):
        self.history.append(record)
        self.records[record["id"]] = record
        if self.paused:
            return
        self.add_item(record)
        self.apply_filters()
        self.list.scrollToBottom()

    def add_item(self, record):
        item = QListWidgetItem()
        item.setData(Qt.ItemDataRole.UserRole, record["id"])
        item.setText(
            f"{record['title']}\n"
            f"{record['preview']}\n"
            f"{record['meta']}"
        )
        item.setToolTip("Нажмите, чтобы посмотреть полный пакет")
        item.setSizeHint(item.sizeHint())
        self.list.addItem(item)

    def rebuild_list(self):
        self.list.clear()
        for record in self.history:
            self.add_item(record)
        self.apply_filters()

    def on_current_item_changed(self, item, previous=None):
        if item is None:
            self.selected_id = None
            self.details_title.setText("Пакет не выбран")
            self.details.clear()
            self.copy_hex.setEnabled(False)
            self.copy_frame.setEnabled(False)
            return
        record = self.records.get(item.data(Qt.ItemDataRole.UserRole))
        if record:
            self.selected_id = record["id"]
            self.show_details(record)

    def show_details(self, record):
        self.details_title.setText(f"MSG-{record['id']:06d}")

        if record.get("system"):
            self.details.setPlainText(record["preview"])
            self.copy_hex.setEnabled(False)
            self.copy_frame.setEnabled(False)
            return

        if record["kind"] == "TX":
            payload = record["payload"]
            self.details.setPlainText(
                f"MESSAGE ID\n{record['id']:06d}\n\n"
                f"Время\n{record['time']}\n\n"
                f"Направление\nTX\n\n"
                f"Тип\nUART / Serial\n\n"
                f"Размер\n{len(record['raw'])} байт\n\n"
                f"TEXT\n{payload.decode('utf-8', errors='replace')}\n\n"
                f"TX HEX\n{record['raw'].hex(' ')}"
            )
            self.copy_hex.setEnabled(True)
            self.copy_frame.setEnabled(True)
            return

        if not record.get("valid"):
            self.details.setPlainText(
                f"MESSAGE ID\n{record['id']:06d}\n\n"
                f"СТАТУС\nНекорректный пакет\n\n"
                f"Размер\n{len(record['raw'])} байт\n\n"
                f"HEX\n{record['raw'].hex(' ')}"
            )
            self.copy_hex.setEnabled(True)
            self.copy_frame.setEnabled(True)
            return

        payload = record["payload"]
        radio = record["raw"]
        serial_frame = bytes([MARK_DATA, len(radio) & 0xFF, (len(radio) >> 8) & 0xFF]) + radio

        self.details.setPlainText(
            f"MESSAGE ID\n{record['id']:06d}\n\n"
            f"Время\n{record['time']}\n\n"
            f"DEVICE ID\n{record['device_id']}\n\n"
            f"PACKET NUMBER\n{record['packet_number']}\n\n"
            f"TYPE\n{record['type']} · {self.type_name(record)}\n\n"
            f"TYPE INPUT\n{record['type_input']}\n\n"
            f"SIZE DATA\n{record['size_data']} байт\n\n"
            f"LONGITUDE\n{record['longitude']}\n\n"
            f"LATITUDE\n{record['latitude']}\n\n"
            f"PAYLOAD\n{self.decode_payload(record)}\n\n"
            f"PAYLOAD HEX\n{payload.hex(' ')}\n\n"
            f"RADIO PACKET HEX\n{radio.hex(' ')}\n\n"
            f"SERIAL FRAME HEX\n{serial_frame.hex(' ')}"
            + (f"\n\nTRAILING DATA\n{record['trailing'].hex(' ')}" if record.get("trailing") else "")
        )
        self.copy_hex.setEnabled(True)
        self.copy_frame.setEnabled(True)

    def apply_filters(self):
        query = self.search.text().strip().lower()
        selected_type = self.type_filter.currentData()

        for i in range(self.list.count()):
            item = self.list.item(i)
            record = self.records.get(item.data(Qt.ItemDataRole.UserRole), {})
            text = (
                f"{record.get('id', '')} {record.get('title', '')} "
                f"{record.get('preview', '')} {record.get('meta', '')} "
                f"{record.get('device_id', '')}"
            ).lower()
            match_text = not query or query in text
            match_type = selected_type == -1 or record.get("type") == selected_type
            item.setHidden(not (match_text and match_type))

    def toggle_pause(self):
        self.paused = not self.paused
        self.pause_button.setText("Продолжить" if self.paused else "Пауза")
        self.pause_label.setText("Лента на паузе" if self.paused else "Лента активна")
        if not self.paused:
            self.rebuild_list()

    def clear_history(self):
        self.history.clear()
        self.records.clear()
        self.list.clear()
        self.selected_id = None
        self.details_title.setText("Пакет не выбран")
        self.details.clear()
        self.copy_hex.setEnabled(False)
        self.copy_frame.setEnabled(False)

    def export_history(self):
        if not self.history:
            self.add_system("История пуста", True)
            return
        path, _ = QFileDialog.getSaveFileName(self, "Экспорт истории", "uart_history.json", "JSON (*.json)")
        if not path:
            return
        data = []
        for record in self.history:
            item = dict(record)
            for key, value in item.items():
                if isinstance(value, bytes):
                    item[key] = value.hex(" ")
            data.append(item)
        try:
            with open(path, "w", encoding="utf-8") as file:
                json.dump(data, file, ensure_ascii=False, indent=2)
            self.add_system(f"Экспорт завершён: {path}")
        except OSError as exc:
            self.error_count += 1
            self.add_system(f"Ошибка экспорта: {exc}", True)
            self.update_status()

    def current_record(self):
        return self.records.get(self.selected_id)

    def copy_selected_hex(self):
        record = self.current_record()
        if record and not record.get("system"):
            QApplication.clipboard().setText(record["raw"].hex(" "))

    def copy_selected_frame(self):
        record = self.current_record()
        if not record or record.get("system"):
            return
        if record["kind"] == "TX":
            frame = record["raw"]
        else:
            radio = record["raw"]
            frame = bytes([MARK_DATA, len(radio) & 0xFF, (len(radio) >> 8) & 0xFF]) + radio
        QApplication.clipboard().setText(frame.hex(" "))

    def new_id(self):
        value = self.next_id
        self.next_id += 1
        return value

    @staticmethod
    def now():
        return datetime.now().strftime("%H:%M:%S")

    def update_status(self):
        connected = bool(self.ser and self.ser.is_open)
        self.status.setText("● Подключено" if connected else "● Отключено")
        self.status.setProperty("connected", "true" if connected else "false")
        self.status.style().unpolish(self.status)
        self.status.style().polish(self.status)
        self.rx_label.setText(f"RX: {self.rx_count}")
        self.tx_label.setText(f"TX: {self.tx_count}")
        self.bytes_label.setText(f"Данные: RX {self.rx_bytes} B · TX {self.tx_bytes} B")
        self.errors_label.setText(f"Ошибки: {self.error_count}")

    def closeEvent(self, event):
        self.disconnect()
        event.accept()


def main():
    app = QApplication(sys.argv)
    app.setStyle("Fusion")
    window = Messenger()
    window.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())