#ifndef _DPE_H_
#define _DPE_H_
#include "main.h"

#define CUDA_SAFE_CALL(error) CudaSafeCall(error, __FILE__, __LINE__)
#define CUDA_CHECK_ERROR() CudaCheckError(__FILE__, __LINE__)
#define M_PI 3.14159265358979323846

using namespace boost::filesystem;

cv::Mat Roberts(const cv::Mat& srcImage);

void Connect(const cv::Mat& dstImage, cv::Mat &labImg, std::vector<int> &labelcnt);

cv::Mat EdgeSegment(const int scale, const cv::Mat& srcImage, int mode = 0, bool use_canny = false, bool high_res_img = true);

void CudaSafeCall(const cudaError_t error, const std::string& file, const int line);

void CudaCheckError(const char* file, const int line);

bool ReadBinMat(const path &mat_path, cv::Mat &mat);

bool WriteBinMat(const path &mat_path, const cv::Mat &mat);

bool ReadCamera(const path &cam_path, Camera &cam);

bool ShowDepthMap(const path &depth_path, const cv::Mat& depth, float depth_min, float depth_max);

bool ShowNormalMap(const path &normal_path, const cv::Mat &normal);

bool ShowWeakImage(const path &weak_path, const cv::Mat &weak);

bool ShowEdgeImage(const path &edge_path, const cv::Mat &edge);

bool ExportPointCloud(const path& point_cloud_path, std::vector<PointList>& pointcloud);

void ExportDepthImagePointCloud(const path& point_cloud_path, const path& image_path, const path& cam_path, cv::Mat& depth, float depth_min, float depth_max);

std::string ToFormatIndex(int index);

template <typename TYPE>
void RescaleMatToTargetSize(const cv::Mat &src, cv::Mat &dst, const cv::Size2i &target_size);

struct ViewState {
	cv::Mat depth;
	cv::Mat normal;
	cv::Mat weak;
	cv::Mat selected_views;
};

class StateStore {
public:
	void Put(int image_id, const cv::Mat &depth, const cv::Mat &normal,
		const cv::Mat &weak, const cv::Mat &selected_views);
	const ViewState* Get(int image_id) const;
	bool Has(int image_id) const;
private:
	std::unordered_map<int, ViewState> states_;
};

class SceneCache {
public:
	explicit SceneCache(const path &dense_folder);
	const cv::Mat& GetGrayImage(int image_id);
	const cv::Mat& GetGrayFloatImage(int image_id, int scale_size);
	Camera GetCamera(int image_id, int target_width, int target_height);
	void SetActiveScale(int scale_size);
	size_t ImageBytes() const;
private:
	path image_folder_;
	path camera_folder_;
	int active_scale_size_;
	std::unordered_map<int, cv::Mat> gray_images_;
	std::unordered_map<int, Camera> cameras_;
	std::unordered_map<int, cv::Mat> active_float_images_;
};

class GpuWorkspace {
public:
	GpuWorkspace();
	~GpuWorkspace();
	void* Acquire(size_t bytes);
	void Release(void *ptr);
	cudaArray* AcquireArray(int width, int height);
	void ReleaseArray(cudaArray *array, int width, int height);
	size_t ReservedBytes() const;
private:
	std::multimap<size_t, void*> free_blocks_;
	std::unordered_map<void*, size_t> live_blocks_;
	std::map<std::pair<int, int>, std::vector<cudaArray*> > free_arrays_;
	std::map<cudaArray*, std::pair<int, int> > live_arrays_;
	size_t reserved_bytes_;
};

void RunFusion(const path &dense_folder, const std::vector<Problem> &problems, const StateStore *state_store = nullptr);
void RunFusion_TAT_Intermediate(const path &dense_folder, const std::vector<Problem> &problems);
void RunFusion_TAT_advanced(const path &dense_folder, const std::vector<Problem> &problems);

struct cudaTextureObjects {
	cudaTextureObject_t images[MAX_IMAGES];
};

struct DataPassHelper {
	int width;
	int height;
	int low_width;
	int low_height;
	int ref_index;
	cudaTextureObjects *texture_objects_cuda;
	cudaTextureObjects *texture_depths_cuda;
	Camera *cameras_cuda;
	float4 *plane_hypotheses_cuda;
	curandState *rand_states_cuda;
	unsigned int *selected_views_cuda;
	short2 *neighbours_cuda;
	int *neighbours_map_cuda;
	uchar *weak_info_cuda;
	float *costs_cuda;
	PatchMatchParams *params;
	int2 debug_point;
	bool show_ncc_info;
	float4* fit_plane_hypotheses_cuda;
	int* label_cuda;
	short2 *label_boundary_cuda;
	uchar* weak_reliable_cuda;
	uchar *view_weight_cuda;
	short2 *weak_nearest_strong;
	uchar *edge_cuda;
	uchar *edge_low_res_cuda;
	short2 *edge_neigh_cuda;
	float *complex_cuda;
	int *radius_cuda;
#ifdef DEBUG_COST_LINE
	float *weak_ncc_cost_cuda;
#endif // DEBUG_COST_LINE

};

class DPE {
public:
	DPE(const Problem &problem, SceneCache *scene_cache = nullptr,
		const StateStore *state_store = nullptr, GpuWorkspace *gpu_workspace = nullptr);
	~DPE();

	void InuputInitialization();
	void CudaSpaceInitialization();
	void SupportInitialization();
	void SetDataPassHelperInCuda();
	void RunPatchMatch();
	void QuadraticDepthFilterWeak();
	float4 GetPlaneHypothesis(int r, int c);
	cv::Mat GetEdge();
	cv::Mat GetPixelStates();
	cv::Mat GetSelectedViews();
	cv::Mat GetRadiusMap();
	int GetWidth();
	int GetHeight();
	float GetDepthMin();
	float GetDepthMax();
	float GetLastGpuTimeMs();
private:
	void GenerateWeakFromImage();

	int num_images;
	int width;
	int height;
	int low_width;
	int low_height;
	Problem problem;
	SceneCache *scene_cache;
	const StateStore *state_store;
	GpuWorkspace *gpu_workspace;
	float last_gpu_time_ms;
	// =========================
	// image host and cuda
	std::vector<cv::Mat> images;
	cudaTextureObjects texture_objects_host;
	cudaArray *cuArray[MAX_IMAGES];
	cudaTextureObjects *texture_objects_cuda;
	// =========================
	// depth host and cuda
	std::vector<cv::Mat> depths;
	cudaTextureObjects texture_depths_host;
	cudaArray *cuDepthArray[MAX_IMAGES];
	cudaTextureObjects *texture_depths_cuda;
	// =========================
	// camera host and cuda
	std::vector<Camera> cameras;
	Camera *cameras_cuda;
	// =========================
	// weak info host and cuda
	int weak_count;
	cv::Mat weak_info_host;
	uchar *weak_info_cuda;
	uchar *weak_reliable_cuda;
	short2 *weak_nearest_strong;
	// =========================
	// neighbour host and cuda
	short2 *neighbours_cuda;
	cv::Mat neighbours_map_host;
	int *neigbours_map_cuda;
	// =========================
	// plane hypotheses host and cuda
	float4 *plane_hypotheses_host;
	float4 *plane_hypotheses_cuda;
	float4 *fit_plane_hypotheses_cuda;
	// =========================
	// edge host and cuda
	cv::Mat edge_host;
	cv::Mat edge_low_res_host;
	uchar *edge_cuda;
	uchar *edge_low_res_cuda;
	short2 *edge_neigh_cuda;
	int *radius_cuda;
	// =========================
	cv::Mat label_host;
	int *label_cuda;
	short2 *label_boundary_cuda;
	// =========================
	float *complex_cuda;
	// cost cuda 
	float *costs_cuda;
	// =========================
	// other var
	// params
	PatchMatchParams params_host;
	PatchMatchParams *params_cuda;
	// random states
	curandState *rand_states_cuda;
	// vis info
	cv::Mat selected_views_host;
	unsigned int *selected_views_cuda;
	// for easy data pass
	DataPassHelper helper_host;
	DataPassHelper *helper_cuda;
	// save view weigth
	uchar *view_weight_cuda;
	//export for test
#ifdef DEBUG_COST_LINE
	float *weak_ncc_cost_cuda;
#endif // DEBUG_COST_LINE
};
#endif // !_DPE_H_
