/*
TODO:
- load file in a separate thread ("prefetch")
- can be smarter about the memcpy call instead of doing it row-by-row
:: use util functions caffe_copy, and Blob->offset()
:: don't forget to update netcdf_daa_layer.cu accordingly
- add ability to shuffle filenames if flag is set
*/
#ifdef USE_NETCDF

#include <fstream>  // NOLINT(readability/streams)
#include <string>
#include <vector>

#include "caffe/layers/netcdf_data_layer.hpp"
#include "caffe/util/netcdf.hpp"

namespace caffe {

	template <typename Dtype>
	NetCDFDataLayer<Dtype>::~NetCDFDataLayer<Dtype>() { }

	// Load data and label from netcdf filename into the class property blobs.
	template <typename Dtype>
	void NetCDFDataLayer<Dtype>::LoadNetCDFFileData(const char* filename) {
		int file_id;
	
		//load netcdf file:
		DLOG(INFO) << "Loading NetCDF file: " << filename;
		int retval = nc_open(filename, NC_NOWRITE, &file_id);
		if(retval != 0){
			if(retval == NC_ENOMEM) std::cerr << "Error, out of memory while opening file " << filename;
			if(retval == NC_EHDFERR) std::cerr << "Error, HDF5-error while opening file " << filename;
			if(retval == NC_EDIMMETA) std::cerr << "Error in NetCDF-4 dimension data in  file " << filename;
			DLOG(INFO) << "Error while opening NetCDF file " << filename;
			CHECK(retval) << "Error while opening file " << filename;
		}
		DLOG(INFO) << "Opened NetCDF file " << filename;

		//determine layer size
		int top_size = this->layer_param_.top_size();
		netcdf_blobs_.resize(top_size);

		const int MIN_DATA_DIM = 1;
		const int MAX_DATA_DIM = INT_MAX;
		
		for (int i = 0; i < top_size; ++i) {
			netcdf_blobs_[i] = shared_ptr<Blob<Dtype> >(new Blob<Dtype>());
			netcdf_load_nd_dataset(file_id, netcdf_variables_[this->layer_param_.top(i)], 
									MIN_DATA_DIM, MAX_DATA_DIM, netcdf_blobs_[i].get());
		}
		
		//close the file
		retval = nc_close(file_id);
		if (retval != 0) {
			LOG(FATAL) << "Failed to close NetCDF file: " << filename;
		}
		
		// MinTopBlobs==1 guarantees at least one top blob
		CHECK_GE(netcdf_blobs_[0]->num_axes(), 1) << "Input must have at least 1 axis.";
		//const int num = netcdf_blobs_[0]->shape(0);
		//for (int i = 1; i < top_size; ++i) {
		//	CHECK_EQ(netcdf_blobs_[i]->shape(0), num);
		//}
		
		// Default to identity permutation.
		data_permutation_.clear();
		data_permutation_.resize(netcdf_blobs_[0]->shape(0));
		for (int i = 0; i < netcdf_blobs_[0]->shape(0); i++)
			data_permutation_[i] = i;

		// Shuffle if needed.
		if (this->layer_param_.netcdf_data_param().shuffle()) {
			std::random_shuffle(data_permutation_.begin(), data_permutation_.end());
			DLOG(INFO) << "Successully loaded " << netcdf_blobs_[0]->shape(0) << " rows (shuffled)";
		} else {
			DLOG(INFO) << "Successully loaded " << netcdf_blobs_[0]->shape(0) << " rows";
		}
	}

	template <typename Dtype>
	void NetCDFDataLayer<Dtype>::LayerSetUp(const vector<Blob<Dtype>*>& bottom,
	const vector<Blob<Dtype>*>& top) {
		// Refuse transformation parameters since NetCDF is totally generic.
		CHECK(!this->layer_param_.has_transform_param()) << this->type() << " does not transform data.";
		// Read the source to parse the filenames.
		const string& file_list = this->layer_param_.netcdf_data_param().source();
		LOG(INFO) << "Loading list of NetCDF filenames from: " << file_list;
		netcdf_filenames_.clear();
		std::ifstream input(file_list.c_str());
		if (input.is_open()) {
			std::string line;
			while (input >> line) {
				netcdf_filenames_.push_back(line);
			}
		} else {
			LOG(FATAL) << "Failed to open source file: " << file_list;
		}
		input.close();
		num_files_ = netcdf_filenames_.size();
		current_file_ = 0;
		LOG(INFO) << "Number of NetCDF files: " << num_files_;
		CHECK_GE(num_files_, 1) << "Must have at least 1 NetCDF filename listed in " << file_list;
		
		//get the top size
		const int top_size = this->layer_param_.top_size();
		
		//read list of netcdf variables which should be read from the file
		for(unsigned int i=0; i<top_size; i++){
			string topname=this->layer_param_.top(i);
			if(topname=="data"){
				num_variables_[topname] = this->layer_param_.netcdf_data_param().variable_data_size();
				for(unsigned int j=0; j<num_variables_[topname]; j++){
					netcdf_variables_[topname].push_back(this->layer_param_.netcdf_data_param().variable_data(j));
				}
			}
			else if(topname=="label"){
				num_variables_[topname] = this->layer_param_.netcdf_data_param().variable_label_size();
				for(unsigned int j=0; j<num_variables_[topname]; j++){
					netcdf_variables_[topname].push_back(this->layer_param_.netcdf_data_param().variable_label(j));
				}
			}
			LOG(INFO) << "Number of NetCDF " << topname << " variables: " << num_variables_[topname];
			CHECK_GE(num_variables_[topname], 1) << "Must have at least 1 NetCDF variable for " << topname << " listed.";
		}
		
		//do permutation if necessary
		file_permutation_.clear();
		file_permutation_.resize(num_files_);
		// Default to identity permutation.
		for (int i = 0; i < num_files_; i++) {
			file_permutation_[i] = i;
		}

		// Shuffle if needed. Only first data param is parsed
		if (this->layer_param_.netcdf_data_param().shuffle()) {
			std::random_shuffle(file_permutation_.begin(), file_permutation_.end());
		}

		// Load the first NetCDF file and initialize the line counter.
		LoadNetCDFFileData(netcdf_filenames_[file_permutation_[current_file_]].c_str());
		//current_row_ = 0;
		
		// Reshape blobs. Only first data param is parsed
		const int batch_size = this->layer_param_.netcdf_data_param().batch_size();
		
		//reshape the top-blobs:
		vector<int> top_shape;
		for (int i = 0; i < top_size; ++i) {
			top_shape.resize(1+netcdf_blobs_[i]->num_axes());
			top_shape[0] = batch_size;
			for (int j = 0; j < (top_shape.size()-1); ++j) {
				top_shape[j+1] = netcdf_blobs_[i]->shape(j);
			}
			top[i]->Reshape(top_shape);
			top_shape.clear();
		}
	}

	template <typename Dtype>
	void NetCDFDataLayer<Dtype>::Forward_cpu(const vector<Blob<Dtype>*>& bottom, const vector<Blob<Dtype>*>& top) {
		const int batch_size = this->layer_param_.netcdf_data_param().batch_size();
		
		for (int j = 0; j < this->layer_param_.top_size(); ++j) {
			std::cout << "top[" << j << "] shape: " << top[j]->shape_string() << std::endl;
		}
		
		//check if we need to shuffle:
		if( (current_file_+batch_size) > num_files_ ){
			current_file_ = 0;
			if (this->layer_param_.netcdf_data_param().shuffle()) {
				std::random_shuffle(file_permutation_.begin(), file_permutation_.end());
			}
			DLOG(INFO) << "Looping around to first file.";
		}
		
		for (int i = 0; i < batch_size; ++i) {
			
			std::cout << "reading: " << netcdf_filenames_[file_permutation_[current_file_+i]] << std::endl;
			
			LoadNetCDFFileData(netcdf_filenames_[file_permutation_[current_file_+i]].c_str());
			for (int j = 0; j < this->layer_param_.top_size(); ++j) {
				int data_dim = top[j]->count() / top[j]->shape(0);
				
				//DEBUG
				unsigned int count=netcdf_blobs_[j]->count();
				double norm=0.;
				std::cout << "input " << j << " batch " << i << std::endl;
				std::cout << "COUNT BEFORE : " << count << " data-dim: " << data_dim << std::endl;
				for(unsigned int k=0; k<count; k++){
					norm+=netcdf_blobs_[j]->cpu_data()[k];
				}
				std::cout << "NORM BEFORE: " << norm << std::endl;
				//DEBUG
				
				caffe_copy(data_dim, netcdf_blobs_[j]->cpu_data(), &(top[j]->mutable_cpu_data()[i * data_dim]));
				
				//DEBUG
				count=netcdf_blobs_[j]->count();
				norm=0.;
				std::cout << "COUNT AFTER: " << count << " data-dim: " << data_dim << std::endl;
				for(unsigned int k=0; k<count; k++){
					norm+=netcdf_blobs_[j]->cpu_data()[k];
				}
				std::cout << "NORM AFTER: " << norm << std::endl;
				//DEBUG
			}
			
			//DEBUG
			//std::cout << "Obtained: " << std::endl;
			//unsigned int datacount=top[0]->count()/batch_size;
			//for(unsigned int l=0; l<datacount; l++){
			//	std::cout << top[0]->cpu_data()[l*datacount] << std::endl;
			//}
			//DEBUG
			
		}
		exit(1);
		
		
		current_file_+=batch_size;
	}

#ifdef CPU_ONLY
STUB_GPU_FORWARD(NetCDFDataLayer, Forward);
#endif

INSTANTIATE_CLASS(NetCDFDataLayer);
REGISTER_LAYER_CLASS(NetCDFData);

}  // namespace caffe
#endif //USE_NETCDF
