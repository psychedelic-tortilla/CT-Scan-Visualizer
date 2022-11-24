#include "widget.h"

#define LOG(x) std::cout << x << "\n";

Widget::Widget(QWidget *parent)
    : QWidget(parent),
      ui(new Ui::Widget),
      m_img(QImage(512, 512, QImage::Format_RGB32)),
      m_depthImage(QImage(512, 512, QImage::Format_RGB32)) {
  // Housekeeping
  ui->setupUi(this);
  m_img.fill(qRgb(0, 0, 0));

  // Buttons
  connect(ui->pushButton_loadImage, SIGNAL(clicked()), this, SLOT(LoadImage()));
  connect(ui->pushButton_loadImage3D, SIGNAL(clicked()), this,
          SLOT(LoadImage3D()));
  connect(ui->pushButton_depthBuffer, SIGNAL(clicked()), this,
          SLOT(RenderDepthBuffer()));
  connect(ui->pushButton_render3D, SIGNAL(clicked()), this, SLOT(Render3D()));

  // Horizontal sliders
  connect(ui->horizontalSlider_threshold, SIGNAL(valueChanged(int)), this,
          SLOT(UpdateThresholdValue(int)));
  connect(ui->horizontalSlider_center, SIGNAL(valueChanged(int)), this,
          SLOT(UpdateWindowingCenter(int)));
  connect(ui->horizontalSlider_windowSize, SIGNAL(valueChanged(int)), this,
          SLOT(UpdateWindowingWindowSize(int)));

  // Vertical sliders
  connect(ui->verticalSlider_depth, SIGNAL(valueChanged(int)), this,
          SLOT(UpdateDepthValue(int)));

  // Initial slider values
  ui->horizontalSlider_center->setValue(0);
  ui->horizontalSlider_windowSize->setValue(1200);
  ui->horizontalSlider_threshold->setValue(500);
  ui->verticalSlider_depth->setValue(0);
}

Widget::~Widget() {
  delete ui;
  delete[] m_depthBuffer;
  delete[] m_shaderBuffer;
}

// Private member functions

void Widget::UpdateSliceView() {
  int center = ui->horizontalSlider_center->value();
  int window_size = ui->horizontalSlider_windowSize->value();

  for (int y = 0; y < m_img.height(); ++y) {
    for (int x = 0; x < m_img.width(); ++x) {
      int raw_value = m_ctimage.data()[x + (y * 512)];
      if (MyLib::WindowInputValue(raw_value, center, window_size).error() ==
          ReturnCode::OK) {
        int windowed_value =
            MyLib::WindowInputValue(raw_value, center, window_size).value();
        m_img.setPixel(x, y,
                       qRgb(windowed_value, windowed_value, windowed_value));
      }
    }
  }
  ui->label_imgArea->setPixmap(QPixmap::fromImage(m_img));
}

void Widget::UpdateDepthImage() {
  int depth = ui->verticalSlider_depth->value();
  int threshold = ui->horizontalSlider_threshold->value();
  int center = ui->horizontalSlider_center->value();
  int window_size = ui->horizontalSlider_windowSize->value();

  for (int y = 0; y < m_img.height(); ++y) {
    for (int x = 0; x < m_img.width(); ++x) {
      int raw_value =
          m_ctimage.data()[(x + y * m_img.width()) +
                            (m_img.height() * m_img.width() * depth)];
      if (raw_value > threshold) {
        m_img.setPixel(x, y, qRgb(255, 0, 0));
        continue;
      }
      if (MyLib::WindowInputValue(raw_value, center, window_size).error() ==
          ReturnCode::OK) {
        int windowed_value =
            MyLib::WindowInputValue(raw_value, center, window_size).value();
        m_img.setPixel(x, y,
                       qRgb(windowed_value, windowed_value, windowed_value));
      }
    }
    ui->label_imgArea->setPixmap(QPixmap::fromImage(m_img));
  }
}

// Slots

void Widget::LoadImage() {
  QString img_path = QFileDialog::getOpenFileName(
      this, "Open Image", "../external/images/", "Raw Image Files (*.raw)");
  ReturnCode ret = m_ctimage.load(img_path).error();

  if (ret != ReturnCode::OK) {
    QMessageBox::critical(this, "Error",
                          "The specified file could not be opened!");
    return;
  }
  UpdateSliceView();
}

void Widget::LoadImage3D() {
  QString img_path = QFileDialog::getOpenFileName(
      this, "Open Image", "../external/images", "Raw Image Files (*.raw)");
  ReturnCode ret = m_ctimage.load(img_path).error();

  if (ret != ReturnCode::OK) {
    QMessageBox::critical(this, "Error",
                          "The specified file could not be opened!");
    return;
  }

  UpdateDepthImage();
}

void Widget::UpdateWindowingCenter(const int val) {
  ui->label_sliderCenter->setText("Center: " + QString::number(val));
  UpdateDepthImage();
}

void Widget::UpdateWindowingWindowSize(const int val) {
  ui->label_sliderWSize->setText("Window Size: " + QString::number(val));
  UpdateDepthImage();
}

void Widget::UpdateDepthValue(const int val) {
  ui->label_currentDepth->setText("Depth: " + QString::number(val));
  UpdateDepthImage();
}

void Widget::UpdateThresholdValue(const int val) {
  ui->label_sliderThreshold->setText("Threshold: " + QString::number(val));
  UpdateDepthImage();
}

void Widget::RenderDepthBuffer() {
  QString img_path = QFileDialog::getOpenFileName(
      this, "Open Image", "../external/images", "Raw Image Files (*.raw)");
  ReturnCode ret = m_ctimage.load(img_path).error();

  if (ret != ReturnCode::OK) {
    QMessageBox::critical(this, "Error",
                          "The specified file could not be opened!");
    return;
  }

  ReturnCode ret2 =
      MyLib::CalculateDepthBuffer(m_ctimage.data(), m_depthBuffer,
                                  m_depthImage.width(), m_depthImage.height(),
                                  130, ui->horizontalSlider_threshold->value())
          .error();

  if (ret2 == ReturnCode::BUFFER_EMPTY) {
    QMessageBox::critical(this, "Error", "Depth Buffer is empty!");
    return;
  }

  for (int y = 0; y < m_depthImage.height(); ++y) {
    for (int x = 0; x < m_depthImage.width(); ++x) {
      m_depthImage.setPixel(x, y,
                            qRgb(m_depthBuffer[x + y * m_depthImage.width()],
                                 m_depthBuffer[x + y * m_depthImage.width()],
                                 m_depthBuffer[x + y * m_depthImage.width()]));
    }
  }
  ui->label_image3D->setPixmap(QPixmap::fromImage(m_depthImage));
}

void Widget::Render3D() {
  QString img_path = QFileDialog::getOpenFileName(
      this, "Open Image", "../external/images", "Raw Image Files (*.raw)");
  ReturnCode err = m_ctimage.load(img_path).error();

  if (err != ReturnCode::OK) {
    QMessageBox::critical(this, "Error",
                          "The specified file could not be opened!");
    return;
  }

  ReturnCode ret =
      MyLib::CalculateDepthBuffer(m_ctimage.data(), m_depthBuffer,
                                  m_depthImage.width(), m_depthImage.height(),
                                  130, ui->horizontalSlider_threshold->value())
          .error();
  if (ret == ReturnCode::BUFFER_EMPTY) {
    QMessageBox::critical(this, "Error", "Depth Buffer is empty!");
    return;
  }

  ReturnCode ret2 =
      MyLib::CalculateDepthBuffer3D(m_depthBuffer, m_shaderBuffer,
                                    m_depthImage.width(), m_depthImage.height())
          .error();
  if (ret2 == ReturnCode::BUFFER_EMPTY) {
    QMessageBox::critical(this, "Error", "Shaded Buffer is empty!");
    return;
  }

  for (int y = 0; y < m_depthImage.height(); ++y) {
    for (int x = 0; x < m_depthImage.width(); ++x) {
      m_depthImage.setPixel(x, y,
                            qRgb(m_shaderBuffer[x + y * m_depthImage.width()],
                                 m_shaderBuffer[x + y * m_depthImage.width()],
                                 m_shaderBuffer[x + y * m_depthImage.width()]));
    }
  }
  ui->label_image3D->setPixmap(QPixmap::fromImage(m_depthImage));
}
