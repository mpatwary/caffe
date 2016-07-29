#include <vector>
#include <map>

#include "caffe/layers/box_to_yolo_layer.hpp"


namespace caffe {

template <typename Dtype>
	

void BoxToYoloLayer<Dtype>::Forward_cpu(const vector<Blob<Dtype>*>& bottom,
										const vector<Blob<Dtype>*>& top) 
{
	//some parameters
	unsigned int top_size=bottom.size()
	
	//some layer parameters
	unsigned int num_images = this->layer_param_.boxtoyolo_param().num_images();
	bool masked = this->layer_param_.boxtoyolo_param().masked();
	unsigned int odimx = this->layer_param_.boxtoyolo_param().orig_dimx();
	unsigned int odimy = this->layer_param_.boxtoyolo_param().orig_dimy();
	unsigned int rdimx = this->layer_param_.boxtoyolo_param().reduced_dimx();
	unsigned int rdimy = this->layer_param_.boxtoyolo_param().reduced_dimy();
	unsigned int maxlabels = this->layer_param_.boxtoyolo_param().max_labels_per_segment();
	
	//class related stuff
	bool onehot = this->layer_param_.boxtoyolo_param().one_hot();
	unsigned int numclasses=1;
	//if one hot encoding is used, read in keys for classes
	std::map<int,int> classmap;
	if(onehot){
		numclasses=this->layer_param_.boxtoyolo_param().classlabel_size();
		for(unsigned int c=0; c<numclasses; c++){
			classmap[this->layer_param_.boxtoyolo_param().classlabel(c)]=c;
		}
	}
	
	//is the subdivided image smaller?
	if(rdimx>odimx || rdimy>odimy) DLOG(FATAL) << "The size of the subdivided image cannot be larger than the original image";
	
	//get some dimensions of the bottom blob:
	unsigned int batch_size=bottom[0]->shape(0);
	unsigned int numvars=bottom[0]->shape(1);
	unsigned int maxlabels=bottom[0]->shape(2);
	
	//some sanity checks:
	if( (masked && numvars!=6) || (!masked && numvars<5) ){
		DLOG(FATAL) << "Please specify 6 columns (image_id, xmin, xmax, ymin, ymax, mask)."
	}
	else if( (!masked && numvars!=5) ){
		DLOG(FATAL) << "Please specify 5 columns (image_id, xmin, xmax, ymin, ymax)."
	}
	
	//determine size of output:
	unsigned int yolosize=batch_size*num_images*rdimx*rdimy*maxlabels*(5+numclasses);
	
	//create output blob size:
	vector<int> top_shape;
	for (int i = 0; i < top_size; ++i) {
		top_shape.resize(6);
		top_shape[0] = batch_size;
		top_shape[1] = num_images;
		top_shape[2] = rdimx;
		top_shape[3] = rdimy;
		top_shape[4] = maxlabels;
		top_shape[5] = (5+numclasses);
		
		//reshape output
		top[i]->Reshape(top_shape);
	}
	unsigned int data_size=top[0]->count();
	
	//perform conversion:
	for (int i = 0; i < top_size; ++i) {
		//initialize everything to zero
#ifdef _OPENMP
#pragma omp parallel for
#endif
		for(unsigned int d=0; d<data_size; d++) top[0]->mutable_cpu_data()[d]=0.;
	
		//perform the actual conversion
//#ifdef _OPENMP
//#pragma omp parallel for
//#endif	
//		for(unsigned int b=0; b<batch_size; b++){
//			std::vector<int> index(numvars+1);
//			index[0]=b;
//			index[1]=1;
//			
//		}
	}
}

template <typename Dtype>
void BoxToYoloLayer<Dtype>::Backward_cpu(const vector<Blob<Dtype>*>& top,
										const vector<bool>& propagate_down, 
										const vector<Blob<Dtype>*>& bottom) 
{
  if (!propagate_down[0]) { return; }
  

}


#ifdef CPU_ONLY
STUB_GPU(BoxToYoloLayer);
#endif

INSTANTIATE_CLASS(BoxToYoloLayer);

}  // namespace caffe

