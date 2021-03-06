#include"fog_move.hpp"
#include<opencv2\highgui\highgui.hpp>   //测试用


//最小值滤波，实际上就是一个腐蚀的过程，直接借助opencv的腐蚀函数来做
void min_filter(cv::Mat &src_img, cv::Mat &res_img, int kernel_size)
{
	cv::Mat element = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(2*kernel_size-1, 2*kernel_size-1));
	cv::erode(src_img, res_img, element);
	//cv::imshow("dark_channel", res_img);
}

//获取通道的最小值，好像只能自己遍历做了，我是用指针做的，貌似这样最快
cv::Mat min_BGR(cv::Mat &src_img)
{
	int rows = src_img.rows;
	int cols = src_img.cols;
	
	cv::Mat res = cv::Mat::zeros(cv::Size(cols, rows), CV_8UC1);
	//creat one channel image to save min_rgb
	int channel_num = src_img.channels();
	/*std::cout << channel_num << std::endl;*/
	if (channel_num <= 1)
	{
		std::cout << "channel is just 1, return zeros matrix!" << std::endl;
		return res;
	}
	
	
	for (int i = 0; i < rows; i++)
	{
		uchar *img_adress = src_img.ptr<uchar>(i);   //当前行的地址
		uchar *res_adress = res.ptr<uchar>(i);       //当前行的地址
		for (int j_i = 0,j_r=0; j_r < cols,j_i<cols*channel_num; j_i+=3,j_r++)
		{
			res_adress[j_r] =std::min(std::min(img_adress[j_i],img_adress[j_i+1]),img_adress[j_i+2]);
		}
		
	}
	//std::cout << "done!"<<std::endl;
	return res;

}


//导向滤波函数
cv::Mat guide_filter(cv::Mat &img, cv::Mat p, int r, double eps)
{
	cv::Mat img_32f, p_32f, res;
	img.convertTo(img_32f, CV_32F);
	p.convertTo(p_32f, CV_32F);
	//都转换成32F方便后边做乘法


	cv::Mat i_p, i2;
	cv::multiply(img_32f, p_32f, i_p);
	cv::multiply(p_32f, p_32f, i2);

	int height = img.rows;
	int width = img.cols;
	cv::Size kernel_sz(2*r-1, 2*r-1);

	//计算四个均值滤波，
	cv::Mat m_Img, m_p, m_Ip, m_i2;    
	cv::boxFilter(img_32f, m_Img, CV_32F, kernel_sz);
	cv::boxFilter(p_32f, m_p, CV_32F, kernel_sz);
	cv::boxFilter(img_32f.mul(p_32f), m_Ip, CV_32F, kernel_sz);
	cv::boxFilter(img_32f.mul(img_32f), m_i2, CV_32F, kernel_sz);
	
	//计算ip的协方差和img的方差
	cv::Mat cov_ip = m_Ip - m_Img.mul(m_p);
	cv::Mat var_i = m_i2 - m_Img.mul(m_Img);

	//求a，b
	cv::Mat a, b;
	cv::divide(cov_ip, var_i+eps, a);
	//a = cov_ip / (var_i + eps);
	b = m_p - a.mul(m_Img);

	//对包含像素i的所有a,b做平均(即对a和b做均值滤波)

	cv::Mat mean_a, mean_b;
	cv::boxFilter(a, mean_a, CV_32F, kernel_sz);
	cv::boxFilter(b, mean_b, CV_32F, kernel_sz);

	//计算输出
	res = mean_a.mul(img_32f) + mean_b;
	
	return res;	
}

//getV1和A，其中m是Uint8类型，转换函数里面做
void getV1(cv::Mat &m,int r, double eps,double w,double maxV1,double &A,cv::Point &A_loc,cv::Mat &V1_)
{
	cv::Mat m_32f;
	m.convertTo(m_32f, CV_32F);
	m_32f = m_32f / 255.0;

	cv::Mat V1 = min_BGR(m);  
	//cv::cvtColor(m, V1, CV_BGR2GRAY);
	cv::Mat V1_32f;
	V1.convertTo(V1_32f, CV_32F);
	V1_32f = V1_32f / 255.0;

	cv::Mat V1_min_filter;
	int r_min = (r / 4) < 3 ? 3 : (r / 4);
	min_filter(V1, V1_min_filter, r/4);
	cv::Mat V1_min_32f;
	V1_min_filter.convertTo(V1_min_32f, CV_32F);        
	//因为我写的min_bgr函数是针对uint8的，所以就先做完最小值之后再去除以255
	V1_min_32f = V1_min_32f / 255.0;

	cv::Mat V1_g = fastGuidedFilter(V1_32f, V1_min_32f, r, eps,4);
	//导向滤波以暗通道为原图像，最小值滤波之后暗通道的图像为引导
	//cv::imshow("导向滤波结果", V1_g);
	//std::cout << V1_g(cv::Rect(0, 0, 3, 3)) << std::endl;
	double max, min;
	cv::minMaxLoc(V1_g, &min, &max, NULL, NULL);
	/*std::cout << max << "  " << min << std::endl;*/


	int bins = 1000;
	const int channels[1] = { 0 };
	float midRanges[] = { 0,max };
	int hist_sz[] = { bins };
	cv::Mat dstHist;
	const float *ranges[] = { midRanges };
	
	cv::calcHist(&V1_g,1,channels,cv::Mat(),dstHist,1,hist_sz,ranges,true,false);        //统计直方图
	

	//std::cout << dstHist;
	cv::Mat sum_hist;
	cv::integral(dstHist, sum_hist,CV_32F);      //计算其积分图
	cv::Mat sum_hist2 = sum_hist(cv::Rect(1, 1, 1, 1000))/ V1_g.total();      //把第二列取出来
	
	//std::cout << sum_hist2;

	//存入vector来查找
	std::vector<float> vd;
	vd.assign((float*)sum_hist2.datastart, (float*)sum_hist2.dataend);
	auto it_=std::upper_bound(vd.begin(), vd.end(),0.999);

	double value_0999 = (max - min) / bins*(it_ - vd.begin());  
	//double value_0999 = vd[999];
	//value_0999就是说小于value_0999的像素占了总数的99.9%,也就是找到最大的0.1%的像素的最小值
    //找到这个之后去阈值化图像，就找到了候选大气光A值得候选位置，然后把这个二值化图像作为掩膜
    //去和导向滤波结果相乘，然后再找到最大值得位置去作为A值。

	//利用阈值化来获取掩膜去求V1符合条件的值
	cv::Mat mask_up_value_0999;
	cv::threshold(V1_g, mask_up_value_0999, value_0999,1,CV_THRESH_BINARY);


	//计算三个通道的均值
	std::vector<cv::Mat> bgr;
	cv::split(m_32f, bgr);
	cv::Mat bgr_mean = (bgr[0] + bgr[1] + bgr[2]) / 3.0;

	//均值掩膜
	cv::Mat A_ = bgr_mean.mul(mask_up_value_0999);
	//cv::imshow("A", A_);
	cv::minMaxLoc(A_, NULL, &A, NULL,&A_loc);
	//std::cout <<"A--" << A << std::endl;
	//这个测试通过了，和python算出的最大值是完全一样的
	
	cv::threshold(V1_g, V1_, maxV1, NULL,CV_THRESH_TRUNC);
	
}


cv::Mat deHaze(cv::Mat &img, bool Gamma ,double r , double eps , double w , double maxV1 )
{
	cv::Mat V1,img_32f;
	cv::Point A_loc;
	cv::Vec3b A_3;
	double A;

	
	getV1(img, r, eps, w, maxV1,A,A_loc, V1);
	A_3 = img.at<cv::Vec3b>(A_loc);
	//cv::imshow("V1", V1);
	//std::cout << A << std::endl;

	img.convertTo(img_32f, CV_32F);
	img_32f = img_32f / 255.0;


	//三个通道分离分别计算
	std::vector<cv::Mat>  img_bgr;
	cv::split(img_32f, img_bgr);    

	/*cv::Mat Yb = (img_bgr[0] - V1) / (1 - V1 / A_3.val[0]);
	cv::Mat Yg = (img_bgr[1] - V1) / (1 - V1 / A_3.val[1]);
	cv::Mat Yr = (img_bgr[2] - V1) / (1 - V1 / A_3.val[2]);*/

	//A是三通道的值的话图像要暗得多

	cv::Mat Yb = (img_bgr[0] - V1) / (1 - V1 / A);
	cv::Mat Yg = (img_bgr[1] - V1) / (1 - V1 / A);
	cv::Mat Yr = (img_bgr[2] - V1) / (1 - V1 / A);
	
	//三个通道合并
	img_bgr.clear();
	img_bgr.push_back(Yb);
	img_bgr.push_back(Yg);
	img_bgr.push_back(Yr);
	
	cv::Mat Y;
	cv::merge(img_bgr, Y);
	//std::cout << Y(cv::Rect(0, 0, 4, 4)) << std::endl;

	//这个截断是可以不要的，因为后期要转换为8UC
	cv::threshold(Y, Y, 1, 1, CV_THRESH_TRUNC);   //大于1 的全部置1
	cv::threshold(Y, Y, 0, 0, CV_THRESH_TOZERO);  //小于0的置零，要不转换成UC3的时候就会变成255，有彩色块
	
	//std::cout << Y(cv::Rect(0, 0, 4, 4)) << std::endl;
	cv::Mat Y_;

	if (Gamma)
	{
		cv::Scalar mean=cv::mean(Y);
		//std::cout << mean.val[0];
		double pow = log(0.5) / log((mean.val[0] + mean.val[1] + mean.val[2])/3);
		cv::pow(Y, pow, Y);
	}
	Y = Y*255.0;

	Y.convertTo(Y_, CV_8UC3);
	return Y_;


}

cv::Mat fastGuidedFilter(cv::Mat I_org, cv::Mat p_org, int r, double eps, int s)
{
	/*
	% GUIDEDFILTER   O(N) time implementation of guided filter.
	%
	%   - guidance image: I (should be a gray-scale/single channel image)
	%   - filtering input image: p (should be a gray-scale/single channel image)
	%   - local window radius: r
	%   - regularization parameter: eps
	*/

	cv::Mat I, _I;
	I_org.convertTo(_I, CV_32F);

	cv::resize(_I, I,cv::Size(), 1.0 / s, 1.0 / s, 1);



	cv::Mat p, _p;
	p_org.convertTo(_p, CV_32F);
	//p = _p;  
	cv::resize(_p, p, cv::Size(), 1.0 / s, 1.0 / s, 1);

	//[hei, wid] = size(I);      
	int hei = I.rows;
	int wid = I.cols;

	r = (22 * r + 1) / s + 1;//因为opencv自带的boxFilter（）中的Size,比如9x9,我们说半径为4     

							 //mean_I = boxfilter(I, r) ./ N;      
	cv::Mat mean_I;
	cv::boxFilter(I, mean_I, CV_32F, cv::Size(r, r));

	//mean_p = boxfilter(p, r) ./ N;      
	cv::Mat mean_p;
	cv::boxFilter(p, mean_p, CV_32F, cv::Size(r, r));

	//mean_Ip = boxfilter(I.*p, r) ./ N;      
	cv::Mat mean_Ip;
	cv::boxFilter(I.mul(p), mean_Ip, CV_32F, cv::Size(r, r));

	//cov_Ip = mean_Ip - mean_I .* mean_p; % this is the covariance of (I, p) in each local patch.      
	cv::Mat cov_Ip = mean_Ip - mean_I.mul(mean_p);

	//mean_II = boxfilter(I.*I, r) ./ N;      
	cv::Mat mean_II;
	cv::boxFilter(I.mul(I), mean_II, CV_32F, cv::Size(r, r));

	//var_I = mean_II - mean_I .* mean_I;      
	cv::Mat var_I = mean_II - mean_I.mul(mean_I);

	//a = cov_Ip ./ (var_I + eps); % Eqn. (5) in the paper;         
	cv::Mat a = cov_Ip / (var_I + eps);

	//b = mean_p - a .* mean_I; % Eqn. (6) in the paper;      
	cv::Mat b = mean_p - a.mul(mean_I);

	//mean_a = boxfilter(a, r) ./ N;      
	cv::Mat mean_a;
	cv::boxFilter(a, mean_a, CV_32F, cv::Size(r, r));
	cv::Mat rmean_a;
	resize(mean_a, rmean_a, cv::Size(I_org.cols, I_org.rows), 1);

	//mean_b = boxfilter(b, r) ./ N;      
	cv::Mat mean_b;
	cv::boxFilter(b, mean_b, CV_32F, cv::Size(r, r));
	cv::Mat rmean_b;
	resize(mean_b, rmean_b, cv::Size(I_org.cols, I_org.rows), 1);

	//q = mean_a .* I + mean_b; % Eqn. (8) in the paper;      
	cv::Mat q = rmean_a.mul(_I) + rmean_b;

	return q;
}


cv::Mat GuidedFilter(cv::Mat I, cv::Mat p, int r, double eps)
{
	/*
	% GUIDEDFILTER   O(N) time implementation of guided filter.
	%
	%   - guidance image: I (should be a gray-scale/single channel image)
	%   - filtering input image: p (should be a gray-scale/single channel image)
	%   - local window radius: r
	%   - regularization parameter: eps
	*/

	cv::Mat _I;
	I.convertTo(_I, CV_32F);
	I = _I;

	cv::Mat _p;
	p.convertTo(_p, CV_32F);
	p = _p;

	//[hei, wid] = size(I);    
	int hei = I.rows;
	int wid = I.cols;

	r = 2 * r + 1;//因为opencv自带的boxFilter（）中的Size,比如9x9,我们说半径为4   

				  //mean_I = boxfilter(I, r) ./ N;    
	cv::Mat mean_I;
	cv::boxFilter(I, mean_I, CV_32F, cv::Size(r, r));

	//mean_p = boxfilter(p, r) ./ N;    
	cv::Mat mean_p;
	cv::boxFilter(p, mean_p, CV_32F, cv::Size(r, r));

	//mean_Ip = boxfilter(I.*p, r) ./ N;    
	cv::Mat mean_Ip;
	cv::boxFilter(I.mul(p), mean_Ip, CV_32F, cv::Size(r, r));

	//cov_Ip = mean_Ip - mean_I .* mean_p; % this is the covariance of (I, p) in each local patch.    
	cv::Mat cov_Ip = mean_Ip - mean_I.mul(mean_p);

	//mean_II = boxfilter(I.*I, r) ./ N;    
	cv::Mat mean_II;
	cv::boxFilter(I.mul(I), mean_II, CV_32F, cv::Size(r, r));

	//var_I = mean_II - mean_I .* mean_I;    
	cv::Mat var_I = mean_II - mean_I.mul(mean_I);

	//a = cov_Ip ./ (var_I + eps); % Eqn. (5) in the paper;       
	cv::Mat a = cov_Ip / (var_I + eps);

	//b = mean_p - a .* mean_I; % Eqn. (6) in the paper;    
	cv::Mat b = mean_p - a.mul(mean_I);

	//mean_a = boxfilter(a, r) ./ N;    
	cv::Mat mean_a;
	cv::boxFilter(a, mean_a, CV_32F, cv::Size(r, r));

	//mean_b = boxfilter(b, r) ./ N;    
	cv::Mat mean_b;
	cv::boxFilter(b, mean_b, CV_32F, cv::Size(r, r));

	//q = mean_a .* I + mean_b; % Eqn. (8) in the paper;    
	cv::Mat q = mean_a.mul(I) + mean_b;

	return q;
}



void enhance(cv::Mat src, cv::Mat &out_img, double compress)
{
	cv::Mat src_img;
	if (src.channels() < 2) cv::cvtColor(src, src_img, CV_BayerRG2GRAY);
	else   src_img = src;


	double max, min;
	cv::minMaxLoc(src_img, &min, &max, NULL, NULL);

	int bins = 1000;
	const int channels[1] = { 0 };
	float midRanges[] = { 0,max };
	int hist_sz[] = { bins };
	cv::Mat dstHist;
	const float *ranges[] = { midRanges };

	cv::calcHist(&src_img, 1, channels, cv::Mat(), dstHist, 1, hist_sz, ranges, true, false);

	cv::Mat sum_hist;
	cv::integral(dstHist, sum_hist, CV_32F);      //计算其积分图
	cv::Mat sum_hist2 = sum_hist(cv::Rect(1, 1, 1, 1000)) / src_img.total();      //把第二列取出来

	std::vector<float> vd;
	vd.assign((float*)sum_hist2.datastart, (float*)sum_hist2.dataend);
	auto it_up = std::upper_bound(vd.begin(), vd.end(), 1 - compress);        //最大的%5的边界
	auto it_down = std::lower_bound(vd.begin(), vd.end(), compress);        //最小的%5的边界

																			/*std::cout << "max--" << max << "  min--" << min << std::endl;
																			std::cout << "it--" << it_up - vd.begin() << std::endl;
																			std::cout << "it--" << it_down - vd.begin() << std::endl;*/

	double value_up = (max - min) / bins*(it_up - vd.begin());
	double value_down = (max - min) / bins*(it_down - vd.begin());

	//std::cout << value_up << "  " << value_down;

	//利用阈值两端截断
	cv::threshold(src_img, src_img, value_up, 255, CV_THRESH_TRUNC);
	//cv::threshold(src_img, src_img, value_down, 255, CV_THRESH_TOZERO);

	//灰度拉伸
	out_img = (src_img - value_down) / (value_up - value_down) * 255;
}