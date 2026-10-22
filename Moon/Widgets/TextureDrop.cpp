#include "Widgets/TextureDrop.h"
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QHBoxLayout>
#include <QRegExpValidator>
#include <QPainter>
#include <QDrag>
#include <QMimeData>
#include <QMessageBox>
#include <QPalette>
#include <QDropEvent>
#include <QPixmap>
#include <QUrl>
#include <QFileInfo>
#include "stb_Image/stb_image.h"
namespace MOON {
    TextureDrop::TextureDrop(QWidget* parent, WidgetProperty* prop)
        :PropertyQtWidget(parent, prop), m_isEmpty(true)
    {
        // 开启接受拖拽
        setAcceptDrops(true);
        // 设置固定大小（可自行修改）
        setFixedSize(120, 120);
        // 设置背景风格
        setStyleSheet("background-color: #2c2c2c; border: 1px solid #555; border-radius: 4px;");
        //// 2. 布局
        //QHBoxLayout* mainLayout = new QHBoxLayout(this);
        //mainLayout->setContentsMargins(5, 5, 5, 5);
        //setLayout(mainLayout);
        connect(this, &TextureDrop::textureChanged, this, &TextureDrop::onTextureChanged);
    }

    QVariant TextureDrop::widgetValue()
    {
        return m_texturePath;
    }

    void TextureDrop::setWidgetValue(const QVariant& value)
    {
        // Only show the assigned asset name here: loading every texture of every material
        // when the inspector is rebuilt would stall the UI. The material designer shows the
        // thumbnails; a drop still loads and previews the image.
        m_texturePath = value.toString();
        m_displayPixmap = QPixmap();
        m_isEmpty = m_texturePath.isEmpty();
        update();
    }

    void TextureDrop::setTexture(const QString& path)
    {
        m_texturePath = path;
        QImage img;

        QString ext = QFileInfo(path).suffix().toLower();
        if (ext == "hdr") img = loadHDR(path);
        else if (ext == "exr") img = loadEXR(path);
        else img.load(path);

        if (!img.isNull()) {
            if (ext == "hdr" || ext == "exr")
                img = tonemap(img);
            m_displayPixmap = QPixmap::fromImage(img);
        }

        m_isEmpty = img.isNull();
        update();
    }

    void TextureDrop::clearTexture()
    {
        m_texturePath.clear();
        m_displayPixmap = QPixmap();
        m_isEmpty = true;
        update();
    }

    void TextureDrop::dragEnterEvent(QDragEnterEvent* event)
    {
        QList<QUrl> urls = event->mimeData()->urls();
        if (urls.isEmpty()) return;

        // 取第一个文件
        QString filePath = urls.first().toLocalFile();
        if (filePath.isEmpty()) return;

        // 检查是否是图片
        if (isImageFile(filePath)) {
            setTexture(filePath);
            emit textureChanged(m_texturePath); // 发送贴图改变信号
        }
    }

    void TextureDrop::dropEvent(QDropEvent* event)
    {
        QList<QUrl> urls = event->mimeData()->urls();
        if (urls.isEmpty()) return;

        // 取第一个文件
        QString filePath = urls.first().toLocalFile();
        if (filePath.isEmpty()) return;

        // 检查是否是图片
        if (isImageFile(filePath)) {
            setTexture(filePath);
            emit textureChanged(m_texturePath); // 发送贴图改变信号
        }
    }

    void TextureDrop::paintEvent(QPaintEvent* event)
    {
        QPainter p(this);
        p.fillRect(0, 0, width(), height(), QColor(44, 44, 44));
        p.setPen(QColor(100, 100, 100));
        p.drawRect(0, 0, width() - 1, height() - 1);

        if (m_displayPixmap.isNull()) {
            p.setPen(QColor(170, 170, 170));
            if (!m_texturePath.isEmpty()) {
                const QString fileName = QFileInfo(m_texturePath).fileName();
                p.drawText(rect().adjusted(4, 4, -4, -4), Qt::AlignCenter | Qt::TextWordWrap, fileName);
            }
            else {
                p.drawText(rect(), Qt::AlignCenter, "Drop\nTexture");
            }
            return;
        }

        // 纯手工等比例缩放，不使用任何 scaled() 方法！
        int pixW = m_displayPixmap.width();
        int pixH = m_displayPixmap.height();
        int w = width();
        int h = height();

        float ratio = qMin((float)w / pixW, (float)h / pixH);
        int dstW = pixW * ratio;
        int dstH = pixH * ratio;
        int x = (w - dstW) / 2;
        int y = (h - dstH) / 2;

        p.drawPixmap(x, y, dstW, dstH, m_displayPixmap);
    }

    void TextureDrop::mousePressEvent(QMouseEvent* event)
    {
        //Q_UNUSED(event);
        //clearTexture();
        //emit textureChanged(""); // 发送空路径
    }

    bool TextureDrop::isImageFile(const QString& path)
    {
        QStringList extList = {
        "png","jpg","jpeg","bmp","tga","dds","tif",
        "hdr","exr"
        };
        QString ext = QFileInfo(path).suffix().toLower();
        return extList.contains(ext);
    }
    QImage TextureDrop::loadHDR(const QString& path)
    {
        // 告诉 stb_image 只加载 RGB，转成 float
        int w, h, channels;
        float* data = stbi_loadf(path.toLocal8Bit().constData(), &w, &h, &channels, 3);

        if (!data || w <= 0 || h <= 0 || w > 10000 || h > 10000) {
            stbi_image_free(data);
            return QImage();
        }

        QImage img(w, h, QImage::Format_RGB888);

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                int idx = (y * w + x) * 3;
                float r = data[idx + 0];
                float g = data[idx + 1];
                float b = data[idx + 2];

                // 简单色调映射，防止过曝
                r = r / (1.0f + r);
                g = g / (1.0f + g);
                b = b / (1.0f + b);

                img.setPixel(x, y, qRgb(
                    qBound(0, (int)(r * 255), 255),
                    qBound(0, (int)(g * 255), 255),
                    qBound(0, (int)(b * 255), 255)
                ));
            }
        }

        stbi_image_free(data);
        return img;
    }
    QImage TextureDrop::loadEXR(const QString& path)
    {
        QImage img(256, 256, QImage::Format_RGB32);
        img.fill(QColor(80, 60, 20));
        QPainter p(&img);
        p.setPen(Qt::white);
        p.drawText(img.rect(), Qt::AlignCenter, "EXR\nPreview");
        return img;
    }
    QImage TextureDrop::tonemap(const QImage& hdr)
    {
        QImage res(hdr.size(), QImage::Format_RGB32);
        for (int y = 0; y < hdr.height(); y++) {
            for (int x = 0; x < hdr.width(); x++) {
                QRgb c = hdr.pixel(x, y);
                float r = qRed(c) / 255.0f;
                float g = qGreen(c) / 255.0f;
                float b = qBlue(c) / 255.0f;
                r = r / (1 + r); g = g / (1 + g); b = b / (1 + b);
                res.setPixel(x, y, qRgb(r * 255, g * 255, b * 255));
            }
        }
        return res;
    }
    void TextureDrop::onTextureChanged(const QString& path) {
        OnValueChanged();
    }

}