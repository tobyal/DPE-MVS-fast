#include "main.h"
#include "DPE.h"

using namespace boost::filesystem;

enum class VisMode { NONE, FINAL, ALL };
enum class CheckpointMode { NONE, FINAL, ALL };

struct RuntimeOptions {
	int gpu_index = 0;
	VisMode vis_mode = VisMode::NONE;
	CheckpointMode checkpoint_mode = CheckpointMode::FINAL;
	bool profile = true;
	path output_folder;
};

struct ProfileStats {
	double edge_ms = 0.0;
	double input_ms = 0.0;
	double support_ms = 0.0;
	double cuda_init_ms = 0.0;
	double patchmatch_ms = 0.0;
	double gpu_kernel_ms = 0.0;
	double result_ms = 0.0;
	double checkpoint_ms = 0.0;
	double fusion_ms = 0.0;
	int process_calls = 0;

	void Print(size_t scene_cache_bytes, size_t gpu_reserved_bytes) const {
		std::cout << "\n[DPE Profile Summary]\n"
			<< "  process calls       : " << process_calls << "\n"
			<< "  edge precompute     : " << edge_ms / 1000.0 << " s\n"
			<< "  input/cache lookup  : " << input_ms / 1000.0 << " s\n"
			<< "  support init        : " << support_ms / 1000.0 << " s\n"
			<< "  CUDA init/upload    : " << cuda_init_ms / 1000.0 << " s\n"
			<< "  PatchMatch + D2H    : " << patchmatch_ms / 1000.0 << " s\n"
			<< "  CUDA kernels        : " << gpu_kernel_ms / 1000.0 << " s\n"
			<< "  result assembly/vis : " << result_ms / 1000.0 << " s\n"
			<< "  checkpoints         : " << checkpoint_ms / 1000.0 << " s\n"
			<< "  fusion              : " << fusion_ms / 1000.0 << " s\n"
			<< "  scene cache peak*   : " << scene_cache_bytes / (1024.0 * 1024.0) << " MiB\n"
			<< "  GPU pool reserved   : " << gpu_reserved_bytes / (1024.0 * 1024.0) << " MiB\n"
			<< "  *reported at end of the active scale\n";
	}
};

static double ElapsedMs(const std::chrono::steady_clock::time_point &start) {
	return std::chrono::duration_cast<std::chrono::duration<double, std::milli> >(
		std::chrono::steady_clock::now() - start).count();
}

static RuntimeOptions ParseOptions(int argc, char **argv) {
	RuntimeOptions options;
	bool gpu_set = false;
	for (int i = 2; i < argc; ++i) {
		std::string arg(argv[i]);
		if (arg.rfind("--vis=", 0) == 0) {
			std::string value = arg.substr(6);
			if (value == "none") options.vis_mode = VisMode::NONE;
			else if (value == "final") options.vis_mode = VisMode::FINAL;
			else if (value == "all") options.vis_mode = VisMode::ALL;
			else throw std::runtime_error("Invalid --vis value: " + value);
		} else if (arg.rfind("--checkpoint=", 0) == 0) {
			std::string value = arg.substr(13);
			if (value == "none") options.checkpoint_mode = CheckpointMode::NONE;
			else if (value == "final") options.checkpoint_mode = CheckpointMode::FINAL;
			else if (value == "all") options.checkpoint_mode = CheckpointMode::ALL;
			else throw std::runtime_error("Invalid --checkpoint value: " + value);
		} else if (arg.rfind("--profile=", 0) == 0) {
			std::string value = arg.substr(10);
			options.profile = value != "off" && value != "0";
		} else if (arg.rfind("--output=", 0) == 0) {
			std::string value = arg.substr(9);
			if (value.empty()) throw std::runtime_error("--output requires a non-empty path");
			options.output_folder = path(value);
		} else if (arg == "--output") {
			if (i + 1 >= argc) throw std::runtime_error("--output requires a path");
			options.output_folder = path(argv[++i]);
		} else if (!gpu_set && !arg.empty() && arg[0] != '-') {
			options.gpu_index = std::atoi(arg.c_str());
			gpu_set = true;
		} else {
			throw std::runtime_error("Unknown option: " + arg);
		}
	}
	return options;
}

static void PrintProgressBar(const std::string &label, const int completed, const int total)
{
	const int bar_width = 40;
	const float ratio = total > 0 ? std::min(1.0f, completed / static_cast<float>(total)) : 1.0f;
	const int filled = static_cast<int>(ratio * bar_width + 0.5f);
	std::cout << "[DPE Progress] " << label << " [";
	for (int i = 0; i < bar_width; ++i) {
		std::cout << (i < filled ? '#' : '.');
	}
	std::cout << "] " << std::fixed << std::setprecision(1) << ratio * 100.0f
		<< "% (" << completed << "/" << total << ")" << std::defaultfloat << std::setfill(' ') << std::endl;
}

void GenerateSampleList(const path &dense_folder, const path &output_folder, std::vector<Problem> &problems)
{
	path cluster_list_path = dense_folder / path("pair.txt");
	problems.clear();
	ifstream file(cluster_list_path);
	std::stringstream iss;
	std::string line;

	int num_images;
	iss.clear();
	std::getline(file, line);
	iss.str(line);
	iss >> num_images;

	for (int i = 0; i < num_images; ++i) {
		Problem problem;
		problem.index = i;
		problem.src_image_ids.clear();
		iss.clear();
		std::getline(file, line);
		iss.str(line);
		iss >> problem.ref_image_id;

		problem.dense_folder = dense_folder;
		problem.result_folder = output_folder / path("views") / path(ToFormatIndex(problem.ref_image_id));
		create_directories(problem.result_folder);

		int num_src_images;
		iss.clear();
		std::getline(file, line);
		iss.str(line);
		iss >> num_src_images;
		for (int j = 0; j < num_src_images; ++j) {
			int id;
			float score;
			iss >> id >> score;
			if (score <= 0.0f) {
				continue;
			}
			problem.src_image_ids.push_back(id);
		}
		problems.push_back(problem);
	}
}

bool CheckImages(const std::vector<Problem> &problems, SceneCache &scene_cache) {
	if (problems.size() == 0) {
		return false;
	}
	const cv::Mat &image = scene_cache.GetGrayImage(problems[0].ref_image_id);
	if (image.empty()) {
		return false;
	}
	const int width = image.cols;
	const int height = image.rows;
	for (size_t i = 1; i < problems.size(); ++i) {
		const cv::Mat &next_image = scene_cache.GetGrayImage(problems[i].ref_image_id);
		if (next_image.cols != width || next_image.rows != height) {
			return false;
		}
	}
	return true;
}

void GetProblemEdges(const Problem &problem, SceneCache &scene_cache) {
	std::cout << "Getting image edges: " << std::setw(8) << std::setfill('0') << problem.ref_image_id << "..." << std::endl;
	int scale = 0;
	while((1 << scale) < problem.scale_size) scale++;

	cv::Mat image_uint = scene_cache.GetGrayImage(problem.ref_image_id);
	cv::Mat src_img;
	image_uint.convertTo(src_img, CV_32FC1);
	const float factor = 1.0f / (float)(problem.scale_size);
	const int new_cols = std::round(src_img.cols * factor);
	const int new_rows = std::round(src_img.rows * factor);
	cv::Mat scaled_image_float;
	cv::resize(src_img, scaled_image_float, cv::Size(new_cols, new_rows), 0, 0, cv::INTER_LINEAR);
	scaled_image_float.convertTo(src_img, CV_8UC1);
	std::cout << "size: " << new_cols << "x" << new_rows << "\n";

	if (problem.params.use_edge) {
		// path edge_path = problem.result_folder / path("edges.dmb");
		path edge_path = problem.result_folder / path("edges_" + std::to_string(scale) + ".dmb");
		std::ifstream edge_file(edge_path.string());
		bool edge_exists = edge_file.good();
		edge_file.close();
		if (!edge_exists) {
			std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
			cv::Mat edge = EdgeSegment(scale, src_img, 0, true, problem.params.high_res_img);
			std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
			std::cout << "Fine edge cost time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms" << std::endl;
			WriteBinMat(edge_path, edge);
			if (problem.show_medium_result) {
				path ref_image_edge_path = problem.result_folder / path("rawedge_" + std::to_string(scale) + ".jpg");
				cv::imwrite(ref_image_edge_path.string(), edge);
			}
		}
	}

	if (problem.params.use_label) {
		// path label_path = problem.result_folder / path("labels.dmb");
		path label_path = problem.result_folder / path("labels_" + std::to_string(scale) + ".dmb");
		std::ifstream label_file(label_path.string());
		bool label_exists = label_file.good();
		label_file.close();
		if (!label_exists) {
			std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
			cv::Mat label = EdgeSegment(scale, image_uint, 1, false, problem.params.high_res_img);
			std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
			std::cout << "Coarse edge cost time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms" << std::endl;
			WriteBinMat(label_path, label);
			if (problem.show_medium_result) {
				path ref_image_con_path = problem.result_folder / path("connect_" + std::to_string(scale) + ".jpg");
				cv::imwrite(ref_image_con_path.string(), EdgeSegment(scale, image_uint, -1, false, problem.params.high_res_img));
			}
		}
	}

	std::cout << "Getting image edges: " << std::setw(8) << std::setfill('0') << problem.ref_image_id << " done!" << std::endl;
}

int ComputeRoundNum(const std::vector<Problem> &problems, SceneCache &scene_cache) {
	if (problems.size() == 0) {
		return 0;
	}
	const cv::Mat &image = scene_cache.GetGrayImage(problems[0].ref_image_id);
	if (image.empty()) {
		return 0;
	}
	int max_size = MAX(image.cols, image.rows);
	int round_num = 1;
	while (max_size > 800) {
		max_size /= 2;
		round_num++;
	}
	return round_num;
}


void ProcessProblem(const Problem &problem, SceneCache &scene_cache, StateStore &state_store,
	GpuWorkspace &gpu_workspace, ProfileStats &profile, bool write_checkpoint) {
	std::cout << "Processing image: " << std::setw(8) << std::setfill('0') << problem.ref_image_id << "..." << std::endl;
    std::cout << "iteration: " << problem.iteration << std::endl;
	std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

	DPE dpe(problem, &scene_cache, &state_store, &gpu_workspace);
	auto stage_start = std::chrono::steady_clock::now();
	dpe.InuputInitialization();
	profile.input_ms += ElapsedMs(stage_start);
	stage_start = std::chrono::steady_clock::now();
	dpe.SupportInitialization();
	profile.support_ms += ElapsedMs(stage_start);
	stage_start = std::chrono::steady_clock::now();
	dpe.CudaSpaceInitialization();
	dpe.SetDataPassHelperInCuda();
	profile.cuda_init_ms += ElapsedMs(stage_start);
	stage_start = std::chrono::steady_clock::now();
	dpe.RunPatchMatch();
	profile.patchmatch_ms += ElapsedMs(stage_start);
	profile.gpu_kernel_ms += dpe.GetLastGpuTimeMs();

	stage_start = std::chrono::steady_clock::now();
	int width = dpe.GetWidth(), height = dpe.GetHeight();
	cv::Mat depth = cv::Mat(height, width, CV_32FC1);
	cv::Mat normal = cv::Mat(height, width, CV_32FC3);
	cv::Mat pixel_states = dpe.GetPixelStates();
	for (int r = 0; r < height; ++r) {
		for (int c = 0; c < width; ++c) {
			float4 plane_hypothesis = dpe.GetPlaneHypothesis(r, c);
			depth.at<float>(r, c) = plane_hypothesis.w;
			if (depth.at<float>(r, c) < dpe.GetDepthMin() || depth.at<float>(r, c) > dpe.GetDepthMax()) {
				depth.at<float>(r, c) = 0;
				pixel_states.at<uchar>(r, c) = UNKNOWN;
			}
			normal.at<cv::Vec3f>(r, c) = cv::Vec3f(plane_hypothesis.x, plane_hypothesis.y, plane_hypothesis.z);
		}
	}
	cv::Mat selected_views = dpe.GetSelectedViews();
	state_store.Put(problem.ref_image_id, depth, normal, pixel_states, selected_views);
	profile.result_ms += ElapsedMs(stage_start);

	if (write_checkpoint) {
		stage_start = std::chrono::steady_clock::now();
		WriteBinMat(problem.result_folder / path("depths.dmb"), depth);
		WriteBinMat(problem.result_folder / path("normals.dmb"), normal);
		WriteBinMat(problem.result_folder / path("weak.bin"), pixel_states);
		WriteBinMat(problem.result_folder / path("selected_views.bin"), selected_views);
		profile.checkpoint_ms += ElapsedMs(stage_start);
	}

	if (problem.show_medium_result) {
		stage_start = std::chrono::steady_clock::now();
		path depth_img_path = problem.result_folder / path("depth_" + std::to_string(problem.iteration) + ".jpg");
		path normal_img_path = problem.result_folder / path("normal_" + std::to_string(problem.iteration) + ".jpg");
		path weak_img_path = problem.result_folder / path("weak_" + std::to_string(problem.iteration) + ".jpg");
		ShowDepthMap(depth_img_path, depth, dpe.GetDepthMin(), dpe.GetDepthMax());
		ShowNormalMap(normal_img_path, normal);
		ShowWeakImage(weak_img_path, pixel_states);

		if ((problem.iteration + 1) % 4 == 0) {
			path image_folder = problem.dense_folder / path("images");
			path cam_folder = problem.dense_folder / path("cams");
			path image_path = image_folder / path(ToFormatIndex(problem.ref_image_id) + ".jpg");
			path cam_path = cam_folder / path(ToFormatIndex(problem.ref_image_id) + "_cam.txt");
			path point_cloud_path = problem.result_folder / path("point_" + std::to_string(problem.iteration) + ".ply");
			// path point_cloud_path = problem.result_folder / path("point_test_" + std::to_string(problem.iteration) + ".ply");

			// for (int r = 0; r < height; ++r) for (int c = 0; c < width; ++c) if (pixel_states.at<uchar>(r, c) != STRONG) depth.at<float>(r, c) = 0;
			// ExportDepthImagePointCloud(point_cloud_path, image_path, cam_path, depth, DPE.GetDepthMin(), DPE.GetDepthMax());
			// remove(point_cloud_path);
		}
		profile.result_ms += ElapsedMs(stage_start);
	}
	std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
	std::cout << "Processing image: " << std::setw(8) << std::setfill('0') << problem.ref_image_id << " done!" << std::endl;
	std::cout << "Cost time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms" << std::endl;
	profile.process_calls++;
}

int main(int argc, char **argv) {
	if (argc < 2) {
		std::cerr << "USAGE: DPE input_folder [gpu_index] [--output=output_folder] [--vis=none|final|all] "
			<< "[--checkpoint=none|final|all] [--profile=on|off]\n";
		return EXIT_FAILURE;
	}
	RuntimeOptions options;
	try {
		options = ParseOptions(argc, argv);
	} catch (const std::exception &e) {
		std::cerr << e.what() << "\n";
		return EXIT_FAILURE;
	}
	path dense_folder(argv[1]);
	path output_folder = options.output_folder.empty() ? dense_folder / path(OUT_NAME) : options.output_folder;
	if (!exists(dense_folder / path("images")) || !exists(dense_folder / path("cams")) ||
		!exists(dense_folder / path("pair.txt"))) {
		std::cerr << "Invalid input folder; expected images/, cams/, and pair.txt under: "
			<< dense_folder.string() << "\n";
		return EXIT_FAILURE;
	}
	create_directories(output_folder / path("views"));
	cudaSetDevice(options.gpu_index);
	std::cout << "Input folder : " << dense_folder.string() << "\n"
		<< "Output folder: " << output_folder.string() << "\n"
		<< "Runtime mode: vis="
		<< (options.vis_mode == VisMode::NONE ? "none" : options.vis_mode == VisMode::FINAL ? "final" : "all")
		<< ", checkpoint="
		<< (options.checkpoint_mode == CheckpointMode::NONE ? "none" : options.checkpoint_mode == CheckpointMode::FINAL ? "final" : "all")
		<< ", profile=" << (options.profile ? "on" : "off") << std::endl;

	SceneCache scene_cache(dense_folder);
	StateStore state_store;
	GpuWorkspace gpu_workspace;
	ProfileStats profile;
	// generate problems
	std::vector<Problem> problems;
	GenerateSampleList(dense_folder, output_folder, problems);
	if (!CheckImages(problems, scene_cache)) {
		std::cerr << "Images may error, check it!\n";
		return EXIT_FAILURE;
	}
	int num_images = problems.size();
	std::cout << "There are " << num_images << " problems needed to be processed!" << std::endl;

	int round_num = ComputeRoundNum(problems, scene_cache);
	const int total_edge_steps = round_num * num_images;
	int completed_edge_steps = 0;
	PrintProgressBar("edge precompute", completed_edge_steps, total_edge_steps);
	const int max_scale_size = static_cast<int>(std::pow(2, round_num - 1));
	for (auto &problem : problems) problem.params.max_scale_size = max_scale_size;
	auto edge_start = std::chrono::steady_clock::now();
	for (int i = 0; i < round_num; ++i) {
		const int scale_size = static_cast<int>(std::pow(2, round_num - 1 - i));
		for (auto &problem : problems) {
			problem.scale_size = scale_size;
			problem.show_medium_result = options.vis_mode == VisMode::ALL;
			GetProblemEdges(problem, scene_cache);
			completed_edge_steps++;
			PrintProgressBar("edge precompute", completed_edge_steps, total_edge_steps);
		}
	}
	profile.edge_ms += ElapsedMs(edge_start);

	std::cout << "Round nums: " << round_num << std::endl;
	int iteration_index = 0;
	const int total_patchmatch_steps = round_num * 4 * num_images;
	int completed_patchmatch_steps = 0;
	PrintProgressBar("patchmatch", completed_patchmatch_steps, total_patchmatch_steps);
	for (int i = 0; i < round_num; ++i) {
		const int scale_size = static_cast<int>(std::pow(2, round_num - 1 - i));
		scene_cache.SetActiveScale(scale_size);
		for (auto &problem : problems) {
			problem.iteration = iteration_index;
			problem.scale_size = scale_size;
			problem.params.scale_size = problem.scale_size;
			problem.show_medium_result = options.vis_mode == VisMode::ALL;
			{
				auto &params = problem.params;
				if (i == 0) {
					params.state = FIRST_INIT;
					params.use_APD = false;
					params.use_edge = false;
				} else {
					params.state = REFINE_INIT;
					params.use_APD = true;
					params.use_edge = true;
					params.ransac_threshold = 0.01 - i * 0.00125;
					params.rotate_time = MIN(static_cast<int>(std::pow(2, i)), 4);
				}
				params.geom_consistency = false;
				params.max_iterations = 3;
				params.weak_peak_radius = 6;
			}
			const bool write_checkpoint = options.checkpoint_mode == CheckpointMode::ALL;
			ProcessProblem(problem, scene_cache, state_store, gpu_workspace, profile, write_checkpoint);
			completed_patchmatch_steps++;
			PrintProgressBar("patchmatch round " + std::to_string(i + 1) + "/" + std::to_string(round_num) + " init",
				completed_patchmatch_steps, total_patchmatch_steps);
		}
		iteration_index++;
		for (int j = 0; j < 3; ++j) {
			for (auto &problem : problems) {
				problem.iteration = iteration_index;
				problem.scale_size = scale_size;
				problem.params.scale_size = problem.scale_size;
				const bool final_call = i == round_num - 1 && j == 2;
				problem.show_medium_result = options.vis_mode == VisMode::ALL ||
					(options.vis_mode == VisMode::FINAL && final_call);
				{
					auto &params = problem.params;
					params.state = REFINE_ITER;
					if (i == 0) {
						params.use_APD = false;
						params.use_edge = false;
					} else {
						params.use_APD = true;
						params.use_edge = true;
					}
					params.ransac_threshold = 0.01 - i * 0.00125;
					params.rotate_time = MIN(static_cast<int>(std::pow(2, i)), 4);
					params.geom_consistency = true;
					params.max_iterations = 3;
					params.weak_peak_radius = MAX(4 - 2 * j, 2);
				}
				const bool write_checkpoint = options.checkpoint_mode == CheckpointMode::ALL ||
					(options.checkpoint_mode == CheckpointMode::FINAL && final_call);
				ProcessProblem(problem, scene_cache, state_store, gpu_workspace, profile, write_checkpoint);
				completed_patchmatch_steps++;
				PrintProgressBar("patchmatch round " + std::to_string(i + 1) + "/" + std::to_string(round_num)
					+ " refine " + std::to_string(j + 1) + "/3",
					completed_patchmatch_steps, total_patchmatch_steps);
			}
			iteration_index++;
		}
		std::cout << "Round: " << i << " done\n";
	}

	std::cout << "[DPE Progress] patchmatch done; starting fusion" << std::endl;
	auto fusion_start = std::chrono::steady_clock::now();
	RunFusion(dense_folder, output_folder, problems, &state_store);
	profile.fusion_ms += ElapsedMs(fusion_start);
	if (options.profile) profile.Print(scene_cache.ImageBytes(), gpu_workspace.ReservedBytes());
	std::cout << "All done\n";
	return EXIT_SUCCESS;
}
