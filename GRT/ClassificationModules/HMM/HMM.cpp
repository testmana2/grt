/*
 GRT MIT License
 Copyright (c) <2012> <Nicholas Gillian, Media Lab, MIT>
 
 Permission is hereby granted, free of charge, to any person obtaining a copy of this software 
 and associated documentation files (the "Software"), to deal in the Software without restriction, 
 including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, 
 and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, 
 subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in all copies or substantial 
 portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT 
 LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. 
 IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, 
 WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE 
 SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "HMM.h"

namespace GRT {

//Register the HMM with the classifier base type
RegisterClassifierModule< HMM > HMM::registerModule("HMM");
    
HMM::HMM(UINT hmmType,UINT modelType,UINT delta,bool useScaling,bool useNullRejection)
{
    this->hmmType = hmmType;
    this->modelType = modelType;
    this->delta = delta;
    this->useScaling = useScaling;
    this->useNullRejection = useNullRejection;
    
    //Default discrete setup
	numStates = 10;
	numSymbols = 20;
	maxNumEpochs = 1000;
	minChange = 1.0e-5;
	
    //Default continuous setup
	downsampleFactor = 5;
    committeeSize = 5;
    
    supportsNullRejection = true;
    classifierMode = TIMESERIES_CLASSIFIER_MODE;
    classType = "HMM";
    classifierType = classType;
    debugLog.setProceedingText("[DEBUG HMM]");
    errorLog.setProceedingText("[ERROR HMM]");
    warningLog.setProceedingText("[WARNING HMM]");
}
    
HMM::HMM(const HMM &rhs){
    classifierMode = TIMESERIES_CLASSIFIER_MODE;
    classType = "HMM";
    classifierType = classType;
    debugLog.setProceedingText("[DEBUG HMM]");
    errorLog.setProceedingText("[ERROR HMM]");
    warningLog.setProceedingText("[WARNING HMM]");
    *this = rhs;
}

HMM::~HMM(void)
{
}

HMM& HMM::operator=(const HMM &rhs){
    if( this != &rhs ){
        this->hmmType = rhs.hmmType;
        this->modelType = rhs.modelType;
        this->delta = rhs.delta;
        this->numStates = rhs.numStates;
        this->numSymbols = rhs.numSymbols;
        this->downsampleFactor = rhs.downsampleFactor;
        this->committeeSize = rhs.committeeSize;
        this->discreteModels = rhs.discreteModels;
        this->continuousModels = rhs.continuousModels;
        
        copyBaseVariables( (Classifier*)&rhs );
    }
    return *this;
    
}
    
bool HMM::deepCopyFrom(const Classifier *classifier){
    
    if( classifier == NULL ) return false;
    
    if( this->getClassifierType() == classifier->getClassifierType() ){
        
        //Cast the classifier pointer to an HMM pointer
        HMM *ptr = (HMM*)classifier;
        
        //Copy the HMM variables
        this->hmmType = ptr->hmmType;
        this->modelType = ptr->modelType;
        this->delta = ptr->delta;
        this->numStates = ptr->numStates;
        this->numSymbols = ptr->numSymbols;
        this->downsampleFactor = ptr->downsampleFactor;
        this->committeeSize = ptr->committeeSize;
        this->discreteModels = ptr->discreteModels;
        this->continuousModels = ptr->continuousModels;
        
        //Copy the base variables
        return copyBaseVariables( classifier );
    }
    return false;
}
    
bool HMM::train(ClassificationData trainingData){
    errorLog << "train(ClassificationData trainingData) - The HMM classifier should be trained using the train(TimeSeriesClassificationData &trainingData) method" << endl;
    return false;
}
    
    
bool HMM::train_(TimeSeriesClassificationData &trainingData){
    
    switch( hmmType ){
        case HMM_DISCRETE:
            return train_discrete( trainingData );
            break;
        case HMM_CONTINUOUS:
            return train_continuous( trainingData );
            break;
    }
    
    errorLog << "train_(TimeSeriesClassificationData &trainingData) - Failed to train model, unknown HMM type!" << endl;

	return false;
}
    
bool HMM::train_discrete(TimeSeriesClassificationData &trainingData){
    
    clear();
    
    if( trainingData.getNumSamples() == 0 ){
        errorLog << "train_discrete(TimeSeriesClassificationData &trainingData) - There are no training samples to train the HMM classifer!" << endl;
        return false;
    }
    
    if( trainingData.getNumDimensions() != 1 ){
        errorLog << "train_discrete(TimeSeriesClassificationData &trainingData) - The number of dimensions in the training data must be 1. If your training data is not 1 dimensional then you must quantize the training data using one of the GRT quantization algorithms" << endl;
        return false;
    }
    
    //Reset the HMM
    numInputDimensions = trainingData.getNumDimensions();
    numClasses = trainingData.getNumClasses();
    discreteModels.resize( numClasses );
    classLabels.resize( numClasses );
    
    //Init the models
    for(UINT k=0; k<numClasses; k++){
        discreteModels[k].resetModel(numStates,numSymbols,modelType,delta);
        discreteModels[k].setMaxNumEpochs( maxNumEpochs );
        discreteModels[k].setMinChange( minChange );
    }
    
    //Train each of the models
    for(UINT k=0; k<numClasses; k++){
        //Get the class ID of this gesture
        UINT classID = trainingData.getClassTracker()[k].classLabel;
        classLabels[k] = classID;
        
        //Convert this classes training data into a list of observation sequences
        TimeSeriesClassificationData classData = trainingData.getClassData( classID );
        vector< vector< UINT > > observationSequences;
        if( !convertDataToObservationSequence( classData, observationSequences ) ){
            return false;
        }
        
        //Train the model
        if( !discreteModels[k].train( observationSequences ) ){
            errorLog << "train_discrete(TimeSeriesClassificationData &trainingData) - Failed to train HMM for class " << classID << endl;
            return false;
        }
    }
    
    //Compute the rejection thresholds
    nullRejectionThresholds.resize(numClasses);
    
    for(UINT k=0; k<numClasses; k++){
        //Get the class ID of this gesture
        UINT classID = trainingData.getClassTracker()[k].classLabel;
        classLabels[k] = classID;
        
        //Convert this classes training data into a list of observation sequences
        TimeSeriesClassificationData classData = trainingData.getClassData( classID );
        vector< vector< UINT > > observationSequences;
        if( !convertDataToObservationSequence( classData, observationSequences ) ){
            return false;
        }
        
        //Test the model
        double loglikelihood = 0;
        double avgLoglikelihood = 0;
        for(UINT i=0; i<observationSequences.size(); i++){
            loglikelihood = discreteModels[k].predict( observationSequences[i] );
            avgLoglikelihood += fabs( loglikelihood );
        }
        nullRejectionThresholds[k] = -( avgLoglikelihood / double( observationSequences.size() ) );
    }
    
    //Flag that the model has been trained
    trained = true;
    
    return true;
    
}
    
bool HMM::train_continuous(TimeSeriesClassificationData &trainingData){
    
    clear();
    
    if( trainingData.getNumSamples() == 0 ){
        errorLog << "train_continuous(TimeSeriesClassificationData &trainingData) - There are no training samples to train the CHMM classifer!" << endl;
        return false;
    }
    
    //Reset the CHMM
    numInputDimensions = trainingData.getNumDimensions();
    numClasses = trainingData.getNumClasses();
    classLabels.resize( numClasses );
    for(UINT k=0; k<numClasses; k++){
        classLabels[k] = trainingData.getClassTracker()[k].classLabel;
    }
    
    //Scale the training data if needed
    ranges = trainingData.getRanges();
    if( useScaling ){
        trainingData.scale(0, 1);
    }
    
    //Setup the models, there will be 1 model for each training sample
    const UINT numTrainingSamples = trainingData.getNumSamples();
    continuousModels.resize( numTrainingSamples );
    
    //Train each of the models
    for(UINT k=0; k<numTrainingSamples; k++){
        
        //Init the model
        continuousModels[k].setDownsampleFactor( downsampleFactor );
        continuousModels[k].setModelType( modelType );
        continuousModels[k].setDelta( delta );
        continuousModels[k].enableScaling( false ); //Scaling should always off for the models as we do any scaling in the CHMM
        
        //Train the model
        if( !continuousModels[k].train_( trainingData[k] ) ){
            errorLog << "train_continuous(TimeSeriesClassificationData &trainingData) - Failed to train CHMM for sample " << k << endl;
            return false;
        }
    }
    
    //Flag that the model has been trained
    trained = true;
    
    //Compute any null rejection thresholds if needed
    if( useNullRejection ){
        //Compute the rejection thresholds
        nullRejectionThresholds.resize(numClasses);
    }
    
    return true;
}
    
bool HMM::predict_(VectorDouble &inputVector){
    
    switch( hmmType ){
        case HMM_DISCRETE:
            return predict_discrete( inputVector );
            break;
        case HMM_CONTINUOUS:
            return predict_continuous( inputVector );
            break;
    }
    
    errorLog << "predict_(VectorDouble &inputVector) - Failed to predict, unknown HMM type!" << endl;
    
	return false;
}
    
bool HMM::predict_discrete( VectorDouble &inputVector ){
    
    predictedClassLabel = 0;
	maxLikelihood = -10000;
    
    if( !trained ){
        errorLog << "predict_(VectorDouble &inputVector) - The HMM classifier has not been trained!" << endl;
        return false;
    }
    
	if( inputVector.size() != numInputDimensions ){
        errorLog << "predict_(VectorDouble &inputVector) - The size of the input vector (" << inputVector.size() << ") does not match the num features in the model (" << numInputDimensions << endl;
		return false;
	}
    
    if( classLikelihoods.size() != numClasses ) classLikelihoods.resize(numClasses,0);
    if( classDistances.size() != numClasses ) classDistances.resize(numClasses,0);
    
    double sum = 0;
    bestDistance = -99e+99;
    UINT bestIndex = 0;
    UINT newObservation = (UINT)inputVector[0];
    
    if( newObservation >= numSymbols ){
        errorLog << "predict_(VectorDouble &inputVector) - The new observation is not a valid symbol! It should be in the range [0 numSymbols-1]" << endl;
        return false;
    }
    
	for(UINT k=0; k<numClasses; k++){
		classDistances[k] = discreteModels[k].predict( newObservation );
        
        //Set the class likelihood as the antilog of the class distances
        classLikelihoods[k] = antilog( classDistances[k] );
        
        //The loglikelihood values are negative so we want the values closest to 0
		if( classDistances[k] > bestDistance ){
			bestDistance = classDistances[k];
			bestIndex = k;
		}
        
        sum += classLikelihoods[k];
    }
    
    //Turn the class distances into proper likelihoods
    for(UINT k=0; k<numClasses; k++){
		classLikelihoods[k] /= sum;
    }
    
    maxLikelihood = classLikelihoods[ bestIndex ];
    predictedClassLabel = classLabels[ bestIndex ];
    
    if( useNullRejection ){
        if( maxLikelihood > nullRejectionThresholds[ bestIndex ] ){
            predictedClassLabel = classLabels[ bestIndex ];
        }else predictedClassLabel = GRT_DEFAULT_NULL_CLASS_LABEL;
    }
    
    return true;
}
    
bool HMM::predict_continuous( VectorDouble &inputVector ){
    
    if( !trained ){
        errorLog << "predict_(VectorDouble &inputVector) - The HMM classifier has not been trained!" << endl;
        return false;
    }
    
	if( inputVector.size() != numInputDimensions ){
        errorLog << "predict_(VectorDouble &inputVector) - The size of the input vector (" << inputVector.size() << ") does not match the num features in the model (" << numInputDimensions << endl;
		return false;
	}
    
    //Scale the input vector if needed
    if( useScaling ){
        for(UINT i=0; i<numInputDimensions; i++){
            inputVector[i] = scale(inputVector[i], ranges[i].minValue, ranges[i].maxValue, 0, 1);
        }
    }
    
    if( classLikelihoods.size() != numClasses ) classLikelihoods.resize(numClasses,0);
    if( classDistances.size() != numClasses ) classDistances.resize(numClasses,0);
    
    std::fill(classDistances.begin(),classDistances.end(),0);
    
    bestDistance = -1000;
    UINT bestIndex = 0;
    double minValue = -1000;
    
    const UINT numModels = (UINT)continuousModels.size();
    vector< IndexedDouble > results(numModels);
    for(UINT i=0; i<numModels; i++){
        
        //Run the prediction for this model
        results[i].value = continuousModels[i].predict( inputVector );
        results[i].index = continuousModels[i].getClassLabel();
        
        if( results[i].value < minValue ){
            minValue = results[i].value;
        }
    }
    
    //Sort the results
    std::sort(results.begin(),results.end(),IndexedDouble::sortIndexedDoubleByValueDescending);
    
    //Run the majority vote
    for(UINT i=0; i<committeeSize; i++){
        classDistances[ getClassLabelIndexValue( results[i].index ) ] += 1.0 / (results[i].value / minValue);
    }
    
    //Turn the class distances into likelihoods
    double sum = Util::sum(classDistances);
    for(UINT k=0; k<numClasses; k++){
        classLikelihoods[k] = classDistances[k] / sum;
    }
    
    //Find the maximum label
    for(UINT k=0; k<numClasses; k++){
        if( classDistances[k] > bestDistance ){
            bestDistance = classDistances[k];
            bestIndex = k;
        }
    }
    
    maxLikelihood = classLikelihoods[ bestIndex ];
    predictedClassLabel = classLabels[ bestIndex ];
    
    return true;
}
    
bool HMM::predict_(MatrixDouble &timeseries){
    
    switch( hmmType ){
        case HMM_DISCRETE:
            return predict_discrete( timeseries );
            break;
        case HMM_CONTINUOUS:
            return predict_continuous( timeseries );
            break;
    }
    
    errorLog << "predict_(MatrixDouble &timeseries) - Failed to predict, unknown HMM type!" << endl;
    
	return false;
    
}
    
bool HMM::predict_discrete(MatrixDouble &timeseries){
    
    if( !trained ){
        errorLog << "predict_continuous(MatrixDouble &timeseries) - The HMM classifier has not been trained!" << endl;
        return false;
    }
    
    if( timeseries.getNumCols() != 1 ){
        errorLog << "predict_discrete(MatrixDouble &timeseries) The number of columns in the input matrix must be 1. It is: " << timeseries.getNumCols() << endl;
        return false;
    }
    
    //Covert the matrix double to observations
    const UINT M = timeseries.getNumRows();
    vector<UINT> observationSequence( M );
    
    for(UINT i=0; i<M; i++){
        observationSequence[i] = (UINT)timeseries[i][0];
        
        if( observationSequence[i] >= numSymbols ){
            errorLog << "predict_discrete(VectorDouble &inputVector) - The new observation is not a valid symbol! It should be in the range [0 numSymbols-1]" << endl;
            return false;
        }
    }
    
    if( classLikelihoods.size() != numClasses ) classLikelihoods.resize(numClasses,0);
    if( classDistances.size() != numClasses ) classDistances.resize(numClasses,0);
    
    bestDistance = -99e+99;
    UINT bestIndex = 0;
    double sum = 0;
	for(UINT k=0; k<numClasses; k++){
		classDistances[k] = discreteModels[k].predict( observationSequence );
        
        //Set the class likelihood as the antilog of the class distances
        classLikelihoods[k] = antilog( classDistances[k] );
        
        //The loglikelihood values are negative so we want the values closest to 0
		if( classDistances[k] > bestDistance ){
			bestDistance = classDistances[k];
			bestIndex = k;
		}
        
        sum += classLikelihoods[k];
    }
    
    //Turn the class distances into proper likelihoods
    for(UINT k=0; k<numClasses; k++){
		classLikelihoods[k] /= sum;
    }
    
    maxLikelihood = classLikelihoods[ bestIndex ];
    predictedClassLabel = classLabels[ bestIndex ];
    
    if( useNullRejection ){
        if( maxLikelihood > nullRejectionThresholds[ bestIndex ] ){
            predictedClassLabel = classLabels[ bestIndex ];
        }else predictedClassLabel = GRT_DEFAULT_NULL_CLASS_LABEL;
    }
    
    return true;
}
    
    
bool HMM::predict_continuous(MatrixDouble &timeseries){
    
    if( !trained ){
        errorLog << "predict_continuous(MatrixDouble &timeseries) - The HMM classifier has not been trained!" << endl;
        return false;
    }
    
	if( timeseries.getNumCols() != numInputDimensions ){
        errorLog << "predict_continuous(MatrixDouble &timeseries) - The number of columns in the input matrix (" << timeseries.getNumCols() << ") does not match the num features in the model (" << numInputDimensions << endl;
		return false;
	}
    
    //Scale the input vector if needed
    if( useScaling ){
        const UINT timeseriesLength = timeseries.getNumRows();
        for(UINT j=0; j<numInputDimensions; j++){
            for(UINT i=0; i<timeseriesLength; i++){
                timeseries[i][j] = scale(timeseries[i][j], ranges[j].minValue, ranges[j].maxValue, 0, 1);
            }
        }
    }
    
    if( classLikelihoods.size() != numClasses ) classLikelihoods.resize(numClasses,0);
    if( classDistances.size() != numClasses ) classDistances.resize(numClasses,0);
    
    std::fill(classDistances.begin(),classDistances.end(),0);
    
    bestDistance = -1000;
    UINT bestIndex = 0;
    double minValue = -1000;
    
    const UINT numModels = (UINT)continuousModels.size();
    vector< IndexedDouble > results(numModels);
    for(UINT i=0; i<numModels; i++){
        
        //Run the prediction for this model
        results[i].value = continuousModels[i].predict( timeseries );
        results[i].index = continuousModels[i].getClassLabel();
        
        if( results[i].value < minValue ){
            minValue = results[i].value;
        }
    }
    
    //Sort the results
    std::sort(results.begin(),results.end(),IndexedDouble::sortIndexedDoubleByValueDescending);
    
    //Run the majority vote
    for(UINT i=0; i<committeeSize; i++){
        classDistances[ getClassLabelIndexValue( results[i].index ) ] += 1.0 / (results[i].value / minValue);
    }
    
    //Turn the class distances into likelihoods
    double sum = Util::sum(classDistances);
    for(UINT k=0; k<numClasses; k++){
        classLikelihoods[k] = classDistances[k] / sum;
    }
    
    //Find the maximum label
    for(UINT k=0; k<numClasses; k++){
        if( classDistances[k] > bestDistance ){
            bestDistance = classDistances[k];
            bestIndex = k;
        }
    }
    
    maxLikelihood = classLikelihoods[ bestIndex ];
    predictedClassLabel = classLabels[ bestIndex ];
    
    return true;
}
    
bool HMM::reset(){
    
    //Reset the base class
    Classifier::reset();
    
    switch( hmmType ){
        case HMM_DISCRETE:
            for(size_t i=0; i<discreteModels.size(); i++){
                discreteModels[i].reset();
            }
            break;
        case HMM_CONTINUOUS:
            for(size_t i=0; i<continuousModels.size(); i++){
                continuousModels[i].reset();
            }
            break;
    }
    
    return true;
}
    
bool HMM::clear(){
    
    //Clear the base class
    Classifier::clear();
    
    discreteModels.clear();
    continuousModels.clear();
    
    return true;
}

bool HMM::saveModelToFile( fstream &file ) const{
	
	if(!file.is_open())
	{
		errorLog << "saveModelToFile( fstream &file ) - File is not open!" << endl;
		return false;
	}

	//Write the header info
	file << "HMM_MODEL_FILE_V1.0\n";
    
    //Write the classifier settings to the file
    if( !Classifier::saveBaseSettingsToFile(file) ){
        errorLog <<"saveModelToFile(fstream &file) - Failed to save classifier base settings to file!" << endl;
		return false;
    }
    
    /*
    
	file << "NumStates: " << numStates << endl;
	file << "NumSymbols: " << numSymbols << endl;
	file << "ModelType: " << modelType << endl;
	file << "Delta: " << delta << endl;
    file << "NumRandomTrainingIterations: " << numRandomTrainingIterations << endl;

    if( trained ){
        //Write each of the models
        for(UINT k=0; k<numClasses; k++){
            file << "Model_ID: " << k+1 << endl;
            file << "NumStates: " << models[k].numStates << endl;
            file << "NumSymbols: " << models[k].numSymbols << endl;
            file << "ModelType: " << models[k].modelType << endl;
            file << "Delta: " << models[k].delta << endl;
            file << "Threshold: " << models[k].cThreshold << endl;
            file << "NumRandomTrainingIterations: " << models[k].numRandomTrainingIterations << endl;
            file << "MaxNumIter: " << models[k].maxNumIter << endl;

            file << "A:\n";
            for(UINT i=0; i<models[k].numStates; i++){
                for(UINT j=0; j<models[k].numStates; j++){
                    file<<models[k].a[i][j]<<"\t";
                }file<<endl;
            }

            file << "B:\n";
            for(UINT i=0; i<models[k].numStates; i++){
                for(UINT j=0; j<models[k].numSymbols; j++){
                    file<<models[k].b[i][j]<<"\t";
                }file<<endl;
            }

            file<<"Pi:\n";
            for(UINT i=0; i<models[k].numStates; i++){
             file<<models[k].pi[i]<<"\t";
            }
        }
    }
*/
	return true;
}

bool HMM::loadModelFromFile( fstream &file ){

    clear();
	
	if(!file.is_open())
	{
		errorLog << "loadModelFromFile( fstream &file ) - File is not open!" << endl;
		return false;
	}
/*
	std::string word;
    double value;
    
    file >> word;
    
	//Find the file type header
	if(word != "HMM_MODEL_FILE_V1.0"){
		errorLog << "loadModelFromFile( fstream &file ) - Could not find Model File Header!" << endl;
		return false;
	}
    
    //Load the base settings from the file
    if( !Classifier::loadBaseSettingsFromFile(file) ){
        errorLog << "loadModelFromFile(string filename) - Failed to load base settings from file!" << endl;
        return false;
    }

	file >> word;
	if(word != "NumStates:"){
		errorLog << "loadModelFromFile( fstream &file ) - Could not find NumStates." << endl;
		return false;
	}
	file >> numStates;

	file >> word;
	if(word != "NumSymbols:"){
		errorLog << "loadModelFromFile( fstream &file ) - Could not find NumSymbols." << endl;
		return false;
	}
	file >> numSymbols;

	file >> word;
	if(word != "ModelType:"){
		errorLog << "loadModelFromFile( fstream &file ) - Could not find ModelType." << endl;
		return false;
	}
	file >> modelType;

	file >> word;
	if(word != "Delta:"){
		errorLog << "loadModelFromFile( fstream &file ) - Could not find Delta." << endl;
		return false;
	}
	file >> delta;
    
    file >> word;
	if(word != "NumRandomTrainingIterations:"){
		errorLog << "loadModelFromFile( fstream &file ) - Could not find NumRandomTrainingIterations." << endl;
		return false;
	}
	file >> numRandomTrainingIterations;
    
    //If the HMM has been trained then load the models
    if( trained ){

        //Resize the buffer
        models.resize(numClasses);

        //Load each of the K classes
        for(UINT k=0; k<numClasses; k++){
            UINT modelID;

            file >> word;
            if(word != "Model_ID:"){
                errorLog << "loadModelFromFile( fstream &file ) - Could not find model ID for the " << k+1 << "th model" << endl;
                return false;
            }
            file >> modelID;

            if(modelID-1!=k){
                errorLog << "loadModelFromFile( fstream &file ) - Model ID does not match the current class ID for the " << k+1 << "th model" << endl;
                return false;
            }

            file >> word;
            if(word != "NumStates:"){
                errorLog << "loadModelFromFile( fstream &file ) - Could not find the NumStates for the " << k+1 << "th model" << endl;
                return false;
            }
            file >> models[k].numStates;

            file >> word;
            if(word != "NumSymbols:"){
                errorLog << "loadModelFromFile( fstream &file ) - Could not find the NumSymbols for the " << k+1 << "th model" << endl;
                return false;
            }
            file >> models[k].numSymbols;

            file >> word;
            if(word != "ModelType:"){
                errorLog << "loadModelFromFile( fstream &file ) - Could not find the modelType for the " << k+1 << "th model" << endl;
                return false;
            }
            file >> models[k].modelType;

            file >> word;
            if(word != "Delta:"){
                errorLog << "loadModelFromFile( fstream &file ) - Could not find the Delta for the " << k+1 << "th model" << endl;
                return false;
            }
            file >> models[k].delta;

            file >> word;
            if(word != "Threshold:"){
                errorLog << "loadModelFromFile( fstream &file ) - Could not find the Threshold for the " << k+1 << "th model" << endl;
                return false;
            }
            file >> models[k].cThreshold;

            file >> word;
            if(word != "NumRandomTrainingIterations:"){
                errorLog << "loadModelFromFile( fstream &file ) - Could not find the numRandomTrainingIterations for the " << k+1 << "th model." << endl;
                return false;
            }
            file >> models[k].numRandomTrainingIterations;

            file >> word;
            if(word != "MaxNumIter:"){
                errorLog << "loadModelFromFile( fstream &file ) - Could not find the MaxNumIter for the " << k+1 << "th model." << endl;
                return false;
            }
            file >> models[k].maxNumIter;

            models[k].a.resize(models[k].numStates,models[k].numStates);
            models[k].b.resize(models[k].numStates,models[k].numSymbols);
            models[k].pi.resize(models[k].numStates);

            //Load the A, B and Pi matrices
            file >> word;
            if(word != "A:"){
                errorLog << "loadModelFromFile( fstream &file ) - Could not find the A matrix for the "<<k+1<<"th model." << endl;
                return false;
            }

            //Load A
            for(UINT i=0; i<models[k].numStates; i++){
                for(UINT j=0; j<models[k].numStates; j++){
                    file >> value;
                    models[k].a[i][j] = value;
                }
            }

            file >> word;
            if(word != "B:"){
                errorLog << "loadModelFromFile( fstream &file ) - Could not find the B matrix for the "<<k+1<<"th model." << endl;
                return false;
            }

            //Load B
            for(UINT i=0; i<models[k].numStates; i++){
                for(UINT j=0; j<models[k].numSymbols; j++){
                    file >> value;
                    models[k].b[i][j] = value;
                }
            }

            file >> word;
            if(word != "Pi:"){
                errorLog << "loadModelFromFile( fstream &file ) - Could not find the Pi matrix for the "<<k+1<<"th model." << endl;
                return false;
            }

            //Load Pi
            for(UINT i=0; i<models[k].numStates; i++){
                file >> value;
                models[k].pi[i] = value;
            }
        }
        
        maxLikelihood = DEFAULT_NULL_LIKELIHOOD_VALUE;
        bestDistance = DEFAULT_NULL_DISTANCE_VALUE;
        classLikelihoods.resize(numClasses,DEFAULT_NULL_LIKELIHOOD_VALUE);
        classDistances.resize(numClasses,DEFAULT_NULL_DISTANCE_VALUE);
    }
*/
	return true;

}
    
bool HMM::convertDataToObservationSequence( TimeSeriesClassificationData &classData, vector< vector< UINT > > &observationSequences ){
        
    observationSequences.resize( classData.getNumSamples() );
    
    for(UINT i=0; i<classData.getNumSamples(); i++){
        MatrixDouble &timeseries = classData[i].getData();
        observationSequences[i].resize( timeseries.getNumRows() );
        for(UINT j=0; j<timeseries.getNumRows(); j++){
            if( timeseries[j][0] >= numSymbols ){
                errorLog << "train(TimeSeriesClassificationData &trainingData) - Found an observation sequence with a value outside of the symbol range! Value: " << timeseries[j][0] << endl;
                return false;
            }
            observationSequences[i][j] = (UINT)timeseries[j][0];
        }
    }
    
    return true;
}
   
UINT HMM::getHMMType() const{
    return hmmType;
}
    
UINT HMM::getModelType() const{
    return modelType;
}

UINT HMM::getDelta() const{
    return delta;
}
    
UINT HMM::getNumStates() const{
    return numStates;
}

UINT HMM::getNumSymbols() const{
    return numSymbols;
}
    
UINT HMM::getNumRandomTrainingIterations() const{
    return numRandomTrainingIterations;
}

vector< DiscreteHiddenMarkovModel > HMM::getDiscreteModels() const{
    return discreteModels;
}
    
vector< ContinuousHiddenMarkovModel > HMM::getContinuousModels() const{
    return continuousModels;
}
    
bool HMM::setHMMType(const UINT hmmType){
    
    clear();
    
    if( hmmType == HMM_DISCRETE || hmmType == HMM_CONTINUOUS ){
        this->hmmType = hmmType;
        return true;
    }
    
    warningLog << "setHMMType(const UINT hmmType) - Unknown HMM type!" << endl;
    return false;
}

bool HMM::setModelType(const UINT modelType){
    
    clear();
    
    if( modelType == HMM_ERGODIC || modelType == HMM_LEFTRIGHT ){
        this->modelType = modelType;
        return true;
    }
    
    warningLog << "setModelType(const UINT modelType) - Unknown model type!" << endl;
    return false;
}

bool HMM::setDelta(const UINT delta){
    
    clear();
    
    if( delta > 0 ){
        this->delta = delta;
        return true;
    }
    
    warningLog << "setDelta(const UINT delta) - Delta must be greater than zero!" << endl;
    return false;
}
    
bool HMM::setDownsampleFactor(const UINT downsampleFactor){
    
    clear();
    if( downsampleFactor > 0 ){
        this->downsampleFactor = downsampleFactor;
        return true;
    }
    return false;
}
    
bool HMM::setCommitteeSize(const UINT committeeSize){
    
    if( committeeSize > 0 ){
        this->committeeSize = committeeSize;
        return true;
    }
    
    return false;
}
    
bool HMM::setNumStates(const UINT numStates){
    
    clear();
    
    if( numStates > 0 ){
        this->numStates = numStates;
        return true;
    }
    
    warningLog << "setNumStates(const UINT numStates) - Num states must be greater than zero!" << endl;
    return false;
}
    
bool HMM::setNumSymbols(const UINT numSymbols){
    
    clear();
    
    if( numSymbols > 0 ){
        this->numSymbols = numSymbols;
        return true;
    }
    
    warningLog << "setNumSymbols(const UINT numSymbols) - Num symbols must be greater than zero!" << endl;
    return false;
}

bool HMM::setNumRandomTrainingIterations(const UINT numRandomTrainingIterations){
    
    clear();
    
    if( numRandomTrainingIterations > 0 ){
        this->numRandomTrainingIterations = numRandomTrainingIterations;
        return true;
    }
    
    warningLog << "setMaxNumIterations(const UINT maxNumIter) - The number of random training iterations must be greater than zero!" << endl;
    return false;
}

}//End of namespace GRT