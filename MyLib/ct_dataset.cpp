#include "ct_dataset.h"

// #define SURFACE_POINTS_FROM_BUFFER

CTDataset::CTDataset() :
  m_imgHeight(512),
  m_imgWidth(512),
  m_imgLayers(256),
  m_imgData(new int16_t[m_imgHeight * m_imgWidth * m_imgLayers]),
  m_regionBuffer(new int[m_imgHeight * m_imgWidth * m_imgLayers]{0}),
  m_visitedBuffer(new int[m_imgHeight * m_imgWidth * m_imgLayers]{0}),
  m_surfacePointBuffer(new int[m_imgHeight * m_imgWidth * m_imgLayers]{0}),
  m_depthBuffer(new int[m_imgHeight * m_imgWidth]{m_imgLayers}),
  m_renderedDepthBuffer(new int[m_imgHeight * m_imgWidth]) {
}

CTDataset::~CTDataset() {
  delete[] m_imgData;
  delete[] m_regionBuffer;
  delete[] m_visitedBuffer;
  delete[] m_surfacePointBuffer;
  delete[] m_depthBuffer;
  delete[] m_renderedDepthBuffer;
}

/**
 * @details File location is specified via a GUI window selection
 * @param img_path The file path of the CT image.
 * @return StatusCode::OK if loading was succesfull, else StatusCode::FOPEN_ERROR.
 */
Status CTDataset::load(QString &img_path) {
  QFile img_file(img_path);
  bool fopen = img_file.open(QIODevice::ReadOnly);
  if (!fopen) {
	return Status(StatusCode::FOPEN_ERROR);
  }

  img_file.read(reinterpret_cast<char *>(m_imgData), m_imgHeight * m_imgWidth * m_imgLayers * sizeof(int16_t));
  img_file.close();
  return Status(StatusCode::OK);
}

/**
 * @return Pointer of type in16_t (short) to the image data array
 * @attention Null-checks and bounds-checks are caller's responsiblity
 */
int16_t *CTDataset::Data() const {
  return m_imgData;
}
/**
 * @return Pointer of type int16_t to the non-3D rendered depth buffer
 * @attention Null-checks and bounds-checks are caller's responsiblity
 */
int *CTDataset::GetDepthBuffer() const {
  return m_depthBuffer;
}

/**
 * @return Pointer of type int16_t to the 3d-rendered depth image buffer
 * @attention Null-checks and bounds-checks are caller's responsiblity
 */
int *CTDataset::GetRenderedDepthBuffer() const {
  return m_renderedDepthBuffer;
}

int *CTDataset::GetRegionGrowingBuffer() const {
  return m_regionBuffer;
}

/**
 * @details Windowing maps a desired slice of the raw grey values (in Hounsfield Units) to a an RGB scale from 0 to
 * \255. This makes it possible to highlight regions of interest such as bone, organ tissue or soft tissue which all
 * have a distinct range of HU values.
 * @param input_value The HU value read from the corresponding CT image pixel
 * @param center The center of the range window
 * @param window_size The size of the range window in which to normalize the HU values
 * @return StatusOr<int> depending on the error. Handles out-of-range HU values, center values and
 * window sizes. If
 * no error occured, the windowed HU value cast to an integer will be returned
 */
StatusOr<int> CTDataset::WindowInputValue(const int &input_value, const int &center, const int &window_size) {
  if ((input_value < -1024) || (input_value > 3071)) {
	return StatusOr<int>(Status(StatusCode::HU_OUT_OF_RANGE));
  }

  if ((center < -1024) || (center > 3071)) {
	return StatusOr<int>(Status(StatusCode::CENTER_OUT_OF_RANGE));
  }

  if ((window_size < 1) || (window_size > 4095)) {
	return StatusOr<int>(Status(StatusCode::WIDTH_OUT_OF_RANGE));
  }

  float half_window_size = 0.5f * static_cast<float>(window_size);
  int lower_bound = static_cast<float>(center) - half_window_size;
  int upper_bound = static_cast<float>(center) + half_window_size;

  if (input_value < lower_bound) {
	return StatusOr<int>(0);
  } else if (input_value > upper_bound) {
	return StatusOr<int>(255);
  }

  return StatusOr<int>(std::roundf((input_value - lower_bound) * (255.0f / static_cast<float>(window_size))));
}

/**
 * @details The calculation is accomplished by traversing all image layers for each pixel. If a pixel with an HU
 * value greater than a chosen threshold is reached, its depth value (the number of layer the pixel is on) is written
 * to a buffer. If no value greater than the threshold value was encountered, the maximum depth value is written to
 * the buffer.
 * @param threshold Pixel grey value (HU value) above which the depth value will be buffered.
 * @return StatusCode::OK if the result buffer is not empty, else StatusCode::BUFFER_EMPTY
 */
Status CTDataset::CalculateDepthBuffer(int threshold) {
  for (int y = 0; y < m_imgHeight; ++y) {
	for (int x = 0; x < m_imgWidth; ++x) {
	  for (int d = 0; d < m_imgLayers; ++d) {
		int curr_pt = (x + y * m_imgWidth) + (m_imgHeight * m_imgWidth * d);
		if (m_imgData[curr_pt] >= threshold) {
		  m_depthBuffer[x + y * m_imgWidth] = d;
		  break;
		}
	  }
	}
  }
  if (m_depthBuffer == nullptr) {
	return Status(StatusCode::BUFFER_EMPTY);
  }
  return Status(StatusCode::OK);
}

#ifdef SURFACE_POINTS_FROM_BUFFER
Status CTDataset::CalculateDepthBufferFromRegionGrowing(Eigen::Matrix3d &rotation_mat) {
  // std::fill_n(m_depthBuffer, m_imgWidth * m_imgHeight, m_imgLayers);
  memset(m_depthBuffer, m_imgLayers, m_imgWidth * m_imgHeight);
  if (m_regionBuffer == nullptr) {
	return Status(StatusCode::BUFFER_EMPTY);
  }

  Eigen::Vector3i pt(0, 0, 0);
  Eigen::Vector3d pt_rot(0, 0, 0);
  for (int y = 0; y < m_imgHeight; ++y) {
	for (int x = 0; x < m_imgWidth; ++x) {
	  // m_depthBuffer[x + y * m_imgWidth] = m_imgLayers;
	  for (int d = 0; d < m_imgLayers; ++d) {
		int curr_pt = x + y * m_imgWidth + (m_imgHeight * m_imgWidth * d);
		if (m_surfacePointBuffer[curr_pt] == 1) {
		  pt.x() = x;
		  pt.y() = y;
		  pt.z() = d;
		  pt_rot = rotation_mat * pt.cast<double>();
		  m_depthBuffer[static_cast<int>(pt_rot.x()) + static_cast<int>(pt_rot.y()) * m_imgWidth]
			= static_cast<int>(pt_rot.z());
		  break;
		}
	  }
	}
  }

  if (m_depthBuffer == nullptr) {
	return Status(StatusCode::BUFFER_EMPTY);
  }

  return Status(StatusCode::OK);
}

#else
Status CTDataset::CalculateDepthBufferFromRegionGrowing(Eigen::Matrix3d &rotation_mat) {
  qDebug() << "Calculating depth buffer from region growing!" << "\n";
  std::fill_n(m_depthBuffer, m_imgHeight * m_imgWidth, m_imgLayers);

  if (m_surfacePoints.empty()) {
	qDebug() << "No surface points!" << "\n";
	return Status(StatusCode::BUFFER_EMPTY);
  }

  int buffer_size = 0;
  Eigen::Vector3d pt_rot;
  for (auto &surface_point : m_surfacePoints) {
	pt_rot = rotation_mat * (surface_point.cast<double>() - m_regionVolumeCenter) + m_regionVolumeCenter;
	if (static_cast<int>(pt_rot.x()) < m_imgWidth && static_cast<int>(pt_rot.y()) < m_imgHeight) {
	  m_depthBuffer[static_cast<int>(pt_rot.x()) + static_cast<int>(pt_rot.y()) * m_imgWidth]
		= static_cast<int>(pt_rot.z());
	  ++buffer_size;
	}
  }

  if (m_depthBuffer == nullptr) {
	qDebug() << "Depth buffer empty!" << "\n";
	return Status(StatusCode::BUFFER_EMPTY);
  }

  qDebug() << "RG depth buffer calculated!" << "\n" << "Number of surface points that got rotated: " << buffer_size
		   << "\n";
  return Status(StatusCode::OK);
}
#endif

/**
 * @details The 3D image is rendered by computing the depth-value gradient in x and y for each pixel (in essence,
 * computing the dot product). The step-size
 * of the algorithm is two, i.e. each pixel is compared to it's left and right as well as it's above and below
 * neighbor. The result is then normalized, multiplied by 255 to yield a valid RGB value and written to an ouput buffer.
 * @return StatusCode::OK if the result buffer is not empty, else StatusCode::BUFFER_EMPTY
 */
Status CTDataset::RenderDepthBuffer() {
  if (m_depthBuffer == nullptr) {
	return Status(StatusCode::BUFFER_EMPTY);
  }

  auto s_x = 2;
  auto s_x_sq = s_x * s_x;
  auto s_y = 2;
  auto s_y_sq = s_y * s_y;
  auto s_pow_four = s_x_sq * s_y_sq;
  auto T_x = 0;
  auto T_y = 0;
  auto syTx_sq = 0;
  auto sxTy_sq = 0;
  auto nom = 255 * s_x * s_y;
  double denom = 0;
  double inv = 0;
  int I_ref = 0;

  for (int y = 1; y < m_imgHeight - 1; ++y) {
	for (int x = 1; x < m_imgWidth - 1; ++x) {
	  T_x = m_depthBuffer[(x + 1) + y * m_imgWidth] - m_depthBuffer[(x - 1) + y * m_imgWidth];
	  T_y = m_depthBuffer[x + (y + 1) * m_imgWidth] - m_depthBuffer[x + (y - 1) * m_imgWidth];
	  syTx_sq = s_y_sq * T_x * T_x;
	  sxTy_sq = s_x_sq * T_y * T_y;
	  denom = std::sqrt(syTx_sq + sxTy_sq + s_pow_four);
	  inv = 1 / denom;
	  I_ref = nom * inv;
	  m_renderedDepthBuffer[x + y * m_imgWidth] = I_ref;
	}
  }

  if (m_renderedDepthBuffer == nullptr) {
	qDebug() << "Depth buffer couldn't be rendered!" << "\n";
	return Status(StatusCode::BUFFER_EMPTY);
  }

  qDebug() << "Depth buffer rendered!" << "\n";
  return Status(StatusCode::OK);
}

int CTDataset::GetGreyValue(const Eigen::Vector3i &pt) const {
  return m_imgData[(pt.x() + pt.y() * m_imgWidth) + (m_imgHeight * m_imgWidth * pt.z())];
}

Status CTDataset::FindSurfacePoints() {
  if (m_regionBuffer == nullptr) {
	return Status(StatusCode::BUFFER_EMPTY);
  }
  m_surfacePoints.clear();
  m_minDepth = 255;

  Eigen::Vector3i surface_point(0, 0, 0);
  for (int y = 0; y < m_imgHeight; ++y) {
	for (int x = 0; x < m_imgWidth; ++x) {
	  for (int d = 0; d < m_imgLayers; ++d) {
		int pt = x + y * m_imgWidth + (m_imgHeight * m_imgWidth * d);
		if (m_regionBuffer[pt] == 1) {
		  if (m_regionBuffer[(x - 1) + y * m_imgWidth + (m_imgHeight * m_imgWidth * d)] == 1
			&& m_regionBuffer[(x + 1) + y * m_imgWidth + (m_imgHeight * m_imgWidth * d)] == 1
			&& m_regionBuffer[x + (y - 1) * m_imgWidth + (m_imgHeight * m_imgWidth * d)] == 1
			&& m_regionBuffer[x + (y + 1) * m_imgWidth + (m_imgHeight * m_imgWidth * d)] == 1
			&& m_regionBuffer[x + y * m_imgWidth + (m_imgHeight * m_imgWidth * (d - 1))] == 1
			&& m_regionBuffer[x + y * m_imgWidth + (m_imgHeight * m_imgWidth * (d + 1))] == 1) {
			continue;
		  }
		  surface_point.x() = x;
		  surface_point.y() = y;
		  surface_point.z() = d;
		  m_surfacePoints.push_back(surface_point);

		  if (d < m_minDepth) m_minDepth = d;
		}
	  }
	}
  }
  qDebug() << "Minimum depth: " << m_minDepth << "\n";
  return Status(StatusCode::OK);
}

Status CTDataset::FindPointCloudCenter() {
  if (m_regionBuffer == nullptr) {
	return Status(StatusCode::BUFFER_EMPTY);
  }

  std::vector<Eigen::Vector3i> all_region_points;
  Eigen::Vector3i region_point(0, 0, 0);
  for (int y = 0; y < m_imgHeight; ++y) {
	for (int x = 0; x < m_imgWidth; ++x) {
	  for (int d = 0; d < m_imgLayers; ++d) {
		int pt = x + y * m_imgWidth + (m_imgHeight * m_imgWidth * d);
		if (m_regionBuffer[pt] == 1) {
		  region_point.x() = x;
		  region_point.y() = y;
		  region_point.z() = d;
		  all_region_points.push_back(region_point);
		}
	  }
	}
  }

  int x_tot = 0;
  int y_tot = 0;
  int z_tot = 0;
  for (auto &pt : all_region_points) {
	x_tot += pt.x();
	y_tot += pt.y();
	z_tot += pt.z();
  }

  auto region_size = static_cast<double>(all_region_points.size());

  m_regionVolumeCenter = Eigen::Vector3d(static_cast<double>(x_tot) / region_size,
										 static_cast<double>(y_tot) / region_size,
										 static_cast<double>(z_tot) / region_size);

  return Status(StatusCode::OK);
}

void CTDataset::RegionGrowing3D(Eigen::Vector3i &seed, int threshold) {
  std::fill_n(m_regionBuffer, m_imgHeight * m_imgWidth * m_imgLayers, 0);
  qDebug() << "Starting region growing algorithm!" << "\n";

  std::stack<Eigen::Vector3i> stack;
  std::vector<Eigen::Vector3i> neighbors;

  stack.push(seed);
  while (!stack.empty()) {
	m_regionBuffer[seed.x() + seed.y() * m_imgWidth + (m_imgHeight * m_imgWidth * seed.z())] = 1;
	stack.pop();

	MyLib::FindNeighbors3D(seed, neighbors);
	for (auto &nb : neighbors) {
	  // 0: Voxel has not been visited
	  if (m_regionBuffer[nb.x() + nb.y() * m_imgWidth + (m_imgHeight * m_imgWidth * nb.z())] == 0) {
		// 2: Voxel has been visited
		m_regionBuffer[nb.x() + nb.y() * m_imgWidth + (m_imgHeight * m_imgWidth * nb.z())] = 2;
		if (GetGreyValue(nb) >= threshold) {
		  // 1: Voxel belongs to region
		  m_regionBuffer[nb.x() + nb.y() * m_imgWidth + (m_imgHeight * m_imgWidth * nb.z())] = 1;
		  stack.push(nb);
		}
	  }
	}
	if (!stack.empty()) {
	  seed = stack.top();
	}
  }
  if (FindSurfacePoints().Ok()) {
	qDebug() << m_surfacePoints.size() << " surface points calculated!" << "\n";
  }
  if (FindPointCloudCenter().Ok()) {
	qDebug() << "Centroid: " << m_regionVolumeCenter.x() << m_regionVolumeCenter.y() << m_regionVolumeCenter.z()
			 << "\n";
  }
}

