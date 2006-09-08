/*=========================================================================

Program:   Visualization Toolkit
Module:    vtkCompositeDataPipeline.cxx

Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
All rights reserved.
See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

This software is distributed WITHOUT ANY WARRANTY; without even
the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include "vtkCompositeDataPipeline.h"

#include "vtkAlgorithm.h"
#include "vtkAlgorithmOutput.h"
#include "vtkMultiGroupDataInformation.h"
#include "vtkMultiGroupDataSet.h"
#include "vtkInformation.h"
#include "vtkInformationDoubleKey.h"
#include "vtkInformationIntegerKey.h"
#include "vtkInformationIntegerVectorKey.h"
#include "vtkInformationKey.h"
#include "vtkInformationObjectBaseKey.h"
#include "vtkInformationStringKey.h"
#include "vtkInformationVector.h"
#include "vtkObjectFactory.h"
#include "vtkPolyData.h"
#include "vtkSmartPointer.h"

#include "vtkImageData.h"
#include "vtkRectilinearGrid.h"
#include "vtkStructuredGrid.h"
#include "vtkUniformGrid.h"

vtkCxxRevisionMacro(vtkCompositeDataPipeline, "1.41");
vtkStandardNewMacro(vtkCompositeDataPipeline);

vtkInformationKeyMacro(vtkCompositeDataPipeline,COMPOSITE_DATA_TYPE_NAME,String);
vtkInformationKeyMacro(vtkCompositeDataPipeline,COMPOSITE_DATA_INFORMATION,ObjectBase);
vtkInformationKeyMacro(vtkCompositeDataPipeline,UPDATE_BLOCKS, ObjectBase);

//----------------------------------------------------------------------------
vtkCompositeDataPipeline::vtkCompositeDataPipeline()
{
  this->InLocalLoop = 0;
  this->InformationCache = vtkInformation::New();

  this->GenericRequest = vtkInformation::New();

  this->DataObjectRequest = vtkInformation::New();
  this->DataObjectRequest->Set(vtkDemandDrivenPipeline::REQUEST_DATA_OBJECT());
  // The request is forwarded upstream through the pipeline.
  this->DataObjectRequest->Set(
    vtkExecutive::FORWARD_DIRECTION(), vtkExecutive::RequestUpstream);
      // Algorithms process this request after it is forwarded.
  this->DataObjectRequest->Set(vtkExecutive::ALGORITHM_AFTER_FORWARD(), 1);

  this->InformationRequest = vtkInformation::New();
  this->InformationRequest->Set(vtkDemandDrivenPipeline::REQUEST_INFORMATION());
  // The request is forwarded upstream through the pipeline.
  this->InformationRequest->Set(
    vtkExecutive::FORWARD_DIRECTION(), vtkExecutive::RequestUpstream);
  // Algorithms process this request after it is forwarded.
  this->InformationRequest->Set(vtkExecutive::ALGORITHM_AFTER_FORWARD(), 1);

  this->UpdateExtentRequest = vtkInformation::New();
  this->UpdateExtentRequest->Set(
    vtkStreamingDemandDrivenPipeline::REQUEST_UPDATE_EXTENT());
  // The request is forwarded upstream through the pipeline.
  this->UpdateExtentRequest->Set(
    vtkExecutive::FORWARD_DIRECTION(), vtkExecutive::RequestUpstream);
  // Algorithms process this request before it is forwarded.
  this->UpdateExtentRequest->Set(vtkExecutive::ALGORITHM_BEFORE_FORWARD(), 1);
                            
  this->DataRequest = vtkInformation::New();
  this->DataRequest->Set(REQUEST_DATA());
  // The request is forwarded upstream through the pipeline.
  this->DataRequest->Set(
    vtkExecutive::FORWARD_DIRECTION(), vtkExecutive::RequestUpstream);
  // Algorithms process this request after it is forwarded.
  this->DataRequest->Set(vtkExecutive::ALGORITHM_AFTER_FORWARD(), 1);
              
              
}

//----------------------------------------------------------------------------
vtkCompositeDataPipeline::~vtkCompositeDataPipeline()
{
  this->InformationCache->Delete();

  this->GenericRequest->Delete();
  this->DataObjectRequest->Delete();
  this->InformationRequest->Delete();
  this->UpdateExtentRequest->Delete();
  this->DataRequest->Delete();
}

//----------------------------------------------------------------------------
int vtkCompositeDataPipeline::ProcessRequest(vtkInformation* request,
                                             vtkInformationVector** inInfoVec,
                                             vtkInformationVector* outInfoVec)
{
  if(this->Algorithm && request->Has(REQUEST_INFORMATION()))
    {
    int appendKey = 1;
    vtkInformationKey** keys = request->Get(vtkExecutive::KEYS_TO_COPY());
    if (keys)
      {
      int len = request->Length(vtkExecutive::KEYS_TO_COPY());
      for (int i=0; i<len; i++)
        {
        if (keys[i] == vtkCompositeDataPipeline::COMPOSITE_DATA_INFORMATION())
          {
          appendKey = 0;
          break;
          }
        }
      }
    if (appendKey)
      {
      request->Append(vtkExecutive::KEYS_TO_COPY(), 
                      vtkCompositeDataPipeline::COMPOSITE_DATA_INFORMATION());
      }
      
    return this->Superclass::ProcessRequest(request, inInfoVec,outInfoVec);
    }

  if(this->Algorithm && request->Has(REQUEST_DATA()))
    {
    // Get the output port from which the request was made.
    int outputPort = -1;
    if(request->Has(FROM_OUTPUT_PORT()))
      {
      outputPort = request->Get(FROM_OUTPUT_PORT());
      }


    // UPDATE_BLOCKS() is the key that tells filters downstream which
    // blocks of a composite dataset is available. This consumers will then
    // ask for these blocks if they have to loop (simple filters in the
    // middle)
    int appendKey = 1;
    vtkInformationKey** keys = request->Get(vtkExecutive::KEYS_TO_COPY());
    if (keys)
      {
      int len = request->Length(vtkExecutive::KEYS_TO_COPY());
      for (int i=0; i<len; i++)
        {
        if (keys[i] == vtkCompositeDataPipeline::UPDATE_BLOCKS())
          {
          appendKey = 0;
          break;
          }
        }
      }
    if (appendKey)
      {
      request->Append(vtkExecutive::KEYS_TO_COPY(), 
                      vtkCompositeDataPipeline::UPDATE_BLOCKS());
      }
    }

  // Let the superclass handle other requests.
  return this->Superclass::ProcessRequest(request, inInfoVec, outInfoVec);
}

//----------------------------------------------------------------------------
// Handle REQUEST_DATA_OBJECT
int vtkCompositeDataPipeline::ExecuteDataObject(
  vtkInformation* request, 
  vtkInformationVector** inInfoVec,
  vtkInformationVector* outInfoVec)
{
  int result=1;

  // If the input is composite, allow algorithm to handle
  // REQUEST_DATA_OBJECT only if it can handle composite
  // datasets. Otherwise, the algorithm will get a chance to handle
  // REQUEST_DATA_OBJECT when it is being iterated over.
  int compositePort;
  int shouldIterate = this->ShouldIterateOverInput(compositePort);
  if (!shouldIterate)
    {
    // Invoke the request on the algorithm.
    result = this->CallAlgorithm(request, vtkExecutive::RequestDownstream,
                                 inInfoVec, outInfoVec);
    if (!result)
      {
      return result;
      }
    }

  int i; 
  // Make sure a valid data object exists for all output ports.
  for(i=0; result && i < this->Algorithm->GetNumberOfOutputPorts(); ++i)
    {
    result = this->CheckCompositeData(i, outInfoVec);
    }

  return result;
}

//----------------------------------------------------------------------------
void vtkCompositeDataPipeline::ExecuteDataStart(
  vtkInformation* request,
  vtkInformationVector** inInfoVec,
  vtkInformationVector* outInfoVec)
{
  this->Superclass::ExecuteDataStart(request, inInfoVec, outInfoVec);

  // True when the pipeline is iterating over the current (simple) filter
  // to produce composite output. In this case, ExecuteDataStart() should
  // NOT Initialize() the composite output.
  if (!this->InLocalLoop)
    {
    // Prepare outputs that will be generated to receive new data.
    for(int i=0; i < outInfoVec->GetNumberOfInformationObjects(); ++i)
      {
      vtkInformation* outInfo = outInfoVec->GetInformationObject(i);
      vtkDataObject* data = outInfo->Get(vtkDataObject::DATA_OBJECT());
      if(data && !outInfo->Get(DATA_NOT_GENERATED()))
        {
        data->PrepareForNewData();
        data->CopyInformationFromPipeline(request);
        }
      }
    }
}

//----------------------------------------------------------------------------
// Handle REQUEST_DATA
int vtkCompositeDataPipeline::ExecuteData(vtkInformation* request,
                                          vtkInformationVector** inInfoVec,
                                          vtkInformationVector* outInfoVec)
{
  int result = 1;

  for(int i=0; i < this->Algorithm->GetNumberOfOutputPorts(); ++i)
    {
    vtkInformation* info = outInfoVec->GetInformationObject(i);
    info->Remove(UPDATE_BLOCKS());
    }

  int compositePort;
  if (this->ShouldIterateOverInput(compositePort))
    {
    this->ExecuteSimpleAlgorithm(
      request, inInfoVec, outInfoVec, compositePort);
    }
  else
    {
    result = this->Superclass::ExecuteData(request,inInfoVec,outInfoVec);
    }

  for(int j=0; j < this->Algorithm->GetNumberOfOutputPorts(); ++j)
    {
    vtkInformation* info = this->GetOutputInformation(j);
    vtkObject* dobj= info->Get(vtkDataObject::DATA_OBJECT());
    if (dobj)
      {
      info->Set(UPDATE_BLOCKS(), dobj);
      }
    }

  return result;
}

//----------------------------------------------------------------------------
int vtkCompositeDataPipeline::InputTypeIsValid(
  int port, int index,vtkInformationVector **inInfoVec)
{
  if (this->InLocalLoop)
    {
    return this->Superclass::InputTypeIsValid(port, index, inInfoVec);
    }
  if (!inInfoVec[port])
    {
    return 0;
    }  

  // If we will be iterating over the input on this port, assume that we
  // can handle any input type. The input type will be checked again during
  // each step of the iteration.
  int compositePort;
  if (this->ShouldIterateOverInput(compositePort))
    {
    if (compositePort == port)
      {
      return 1;
      }
    }

  // Otherwise, let superclass handle it.
  return this->Superclass::InputTypeIsValid(port, index, inInfoVec);
}

//----------------------------------------------------------------------------
int vtkCompositeDataPipeline::ShouldIterateOverInput(int& compositePort)
{
  compositePort = -1;
  // Find the first input that has a composite data that does not match
  // the required input type. We assume that that port input has to
  // be iterated over. We also require that this port has only one
  // connection.
  int numInputPorts = this->Algorithm->GetNumberOfInputPorts();
  for(int i=0; i < numInputPorts; ++i)
    {
    int numInConnections = this->Algorithm->GetNumberOfInputConnections(i);
    // If there is 1 connection
    if (numInConnections == 1)
      {
      vtkInformation* inPortInfo = 
        this->Algorithm->GetInputPortInformation(i);
      const char* inputType = 
        inPortInfo->Get(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE());
      if (inputType)
        {
        vtkInformation* inInfo = this->GetInputInformation(i, 0);
        vtkDataObject* input = inInfo->Get(vtkDataObject::DATA_OBJECT());
        // If input does not match required input type
        if (input && !input->IsA(inputType))
          {
          // If input is composite
          if (vtkCompositeDataSet::SafeDownCast(input))
            {
            // Assume that we have to iterate over input
            compositePort = i;
            return 1;
            }
          }
        }
      }
    }
  return 0;
}

//----------------------------------------------------------------------------
vtkDataObject* vtkCompositeDataPipeline::ExecuteSimpleAlgorithmForBlock(
  vtkInformationVector** inInfoVec,
  vtkInformationVector* outInfoVec,
  vtkInformation* inInfo,
  vtkInformation* outInfo,
  vtkInformation* request,  
  vtkDataObject* dobj)
{

  if (dobj->IsA("vtkMultiGroupDataSet"))
    {
    vtkMultiGroupDataSet* output = vtkMultiGroupDataSet::New();

    vtkMultiGroupDataSet* mbInput = vtkMultiGroupDataSet::SafeDownCast(dobj);
    unsigned int numGroups = mbInput->GetNumberOfGroups();
    output->SetNumberOfGroups(numGroups);
    for (unsigned int groupId=0; groupId<numGroups; groupId++)
      {
      unsigned int numBlocks = mbInput->GetNumberOfDataSets(groupId);
      output->SetNumberOfDataSets(groupId, numBlocks);
      for (unsigned int blockId=0; blockId<numBlocks; blockId++)
        {
        vtkDataObject* block = mbInput->GetDataSet(groupId, blockId);
        vtkDataObject* outBlock = 
          this->ExecuteSimpleAlgorithmForBlock(inInfoVec,
                                               outInfoVec,
                                               inInfo,
                                               outInfo,
                                               request,
                                               block);
        output->SetDataSet(groupId, blockId, outBlock);
        outBlock->Delete();
        }
      }
    return output;
    }
  else
    {
    // There must be a bug somehwere. If this Remove()
    // is not called, the following Set() has the effect
    // of removing (!) the key.
    inInfo->Remove(vtkDataObject::DATA_OBJECT());
    inInfo->Set(vtkDataObject::DATA_OBJECT(), dobj);
    
    // Process the whole dataset
    this->CopyFromDataToInformation(dobj, inInfo);
    
    request->Set(REQUEST_DATA_OBJECT());
    this->Superclass::ExecuteDataObject(
      request, this->GetInputInformation(),this->GetOutputInformation());
    request->Remove(REQUEST_DATA_OBJECT());
    
    request->Set(REQUEST_INFORMATION());
    // Make sure that pipeline informations is in sync with the data
    dobj->CopyInformationToPipeline(request, 0, inInfo, 1);
    // This should not be needed but since a lot of image filters do:
    // img->GetScalarType(), it is necessary.
    dobj->GetProducerPort(); // make sure there is pipeline info.
    dobj->CopyInformationToPipeline(
      request, 0, dobj->GetPipelineInformation(), 1);
    this->Superclass::ExecuteInformation(request,inInfoVec,outInfoVec);
    request->Remove(REQUEST_INFORMATION());
    
    for(int m=0; m < this->Algorithm->GetNumberOfOutputPorts(); ++m)
      {
      vtkInformation* info = this->GetOutputInformation(m);
      // Update the whole thing
      if (info->Has(
            vtkStreamingDemandDrivenPipeline::WHOLE_EXTENT()))
        {
        int extent[6] = {0,-1,0,-1,0,-1};
        info->Get(
          vtkStreamingDemandDrivenPipeline::WHOLE_EXTENT(),
          extent);
        info->Set(
          vtkStreamingDemandDrivenPipeline::UPDATE_EXTENT(), 
          extent, 
          6);
        info->Set(
          vtkStreamingDemandDrivenPipeline::UPDATE_EXTENT_INITIALIZED(), 
          1);
        info->Set(
          vtkStreamingDemandDrivenPipeline::UPDATE_NUMBER_OF_PIECES(), 
          1);
        info->Set(
          vtkStreamingDemandDrivenPipeline::UPDATE_PIECE_NUMBER(), 0);
        }
      }
    
    request->Set(REQUEST_UPDATE_EXTENT());
    this->CallAlgorithm(request, vtkExecutive::RequestUpstream,
                        inInfoVec, outInfoVec);
    this->ForwardUpstream(request);
    request->Remove(REQUEST_UPDATE_EXTENT());
    
    request->Set(REQUEST_DATA());
    this->Superclass::ExecuteData(request,inInfoVec,outInfoVec);
    request->Remove(REQUEST_DATA());
    
    vtkDataObject* output = outInfo->Get(vtkDataObject::DATA_OBJECT());
    if (!output)
      {
      return 0;
      }
    vtkDataObject* outputCopy = output->NewInstance();
    outputCopy->ShallowCopy(output);
    return outputCopy;
    }
}

//----------------------------------------------------------------------------
// Execute a simple (non-composite-aware) filter multiple times, once per
// block. Collect the result in a composite dataset that is of the same
// structure as the input.
void vtkCompositeDataPipeline::ExecuteSimpleAlgorithm(
  vtkInformation* request,
  vtkInformationVector** inInfoVec,
  vtkInformationVector* outInfoVec,
  int compositePort)
{
  int outputInitialized = 0;

  this->ExecuteDataStart(request,inInfoVec,outInfoVec);

  vtkInformation* outInfo = 0;
  vtkSmartPointer<vtkDataObject> prevOutput;

  if (this->GetNumberOfOutputPorts() > 0)
    {
    outInfo = outInfoVec->GetInformationObject(0);
    }

  // Make sure a valid composite data object exists for all output ports.
  for(int i=0; i < this->Algorithm->GetNumberOfOutputPorts(); ++i)
    {
    this->CheckCompositeData(i, outInfoVec);
    }

  // Loop using the first input on the first port.
  // This might not be valid for all cases but it is a decent
  // assumption to start with.
  // TODO: Loop over all inputs
  vtkInformation* inInfo = this->GetInputInformation(compositePort, 0);
  vtkCompositeDataSet* input = vtkCompositeDataSet::SafeDownCast(
    inInfo->Get(vtkDataObject::DATA_OBJECT()));

  vtkMultiGroupDataSet* updateInfo = 
    vtkMultiGroupDataSet::SafeDownCast(
      inInfo->Get(vtkCompositeDataPipeline::UPDATE_BLOCKS()));

  vtkSmartPointer<vtkCompositeDataSet> compositeOutput = 
    vtkCompositeDataSet::SafeDownCast(
      outInfo->Get(vtkDataObject::DATA_OBJECT()));

  if (input && updateInfo)
    {
    vtkSmartPointer<vtkInformation> r = 
      vtkSmartPointer<vtkInformation>::New();

    r->Set(FROM_OUTPUT_PORT(), inInfo->GetPort(PRODUCER()));

    // The request is forwarded upstream through the pipeline.
    r->Set(vtkExecutive::FORWARD_DIRECTION(), vtkExecutive::RequestUpstream);
        
    // Algorithms process this request after it is forwarded.
    r->Set(vtkExecutive::ALGORITHM_AFTER_FORWARD(), 1);

    unsigned int numGroups = updateInfo->GetNumberOfGroups();
    vtkSmartPointer<vtkDataObject> prevInput = 
      inInfo->Get(vtkDataObject::DATA_OBJECT());

    vtkDebugMacro("EXECUTING: " << this->Algorithm->GetClassName());

    // True when the pipeline is iterating over the current (simple)
    // filter to produce composite output. In this case,
    // ExecuteDataStart() should NOT Initialize() the composite output.
    this->InLocalLoop = 1;

    // Store the information (whole_extent and maximum_number_of_pieces)
    // before looping. Otherwise, executeinformation will cause
    // changes (because we pretend that the max. number of pieces is
    // one to process the whole block)
    this->PushInformation(inInfo);
    for (unsigned int k=0; k<numGroups; k++)
      {
      unsigned int numDataSets = updateInfo->GetNumberOfDataSets(k);
      for (unsigned l=0; l<numDataSets; l++)
        {
        if (updateInfo->GetDataSet(k,l))
          {
          r->Set(vtkMultiGroupDataSet::GROUP(), k);
          r->Set(vtkCompositeDataSet::INDEX(),    l);
          vtkDataObject* dobj = input->GetDataSet(r);

          vtkDataObject* outCopy = 
            this->ExecuteSimpleAlgorithmForBlock(inInfoVec,
                                                 outInfoVec,
                                                 inInfo,
                                                 outInfo,
                                                 r,
                                                 dobj);
          if (outCopy)
            {
            if (!outputInitialized)
              {
              compositeOutput->PrepareForNewData();
              outputInitialized = 1;
              }
            compositeOutput->AddDataSet(r, outCopy);
            outCopy->Delete();
            }
          }
        }
      }
    // True when the pipeline is iterating over the current (simple)
    // filter to produce composite output. In this case,
    // ExecuteDataStart() should NOT Initialize() the composite output.
    this->InLocalLoop = 0;
    // Restore the extent information and force it to be
    // copied to the output. Composite sources should set
    // MAXIMUM_NUMBER_OF_PIECES to -1 anyway (and handle
    // piece requests properly).
    this->PopInformation(inInfo);
    r->Set(REQUEST_INFORMATION());
    this->CopyDefaultInformation(r, vtkExecutive::RequestDownstream,
                                 this->GetInputInformation(),
                                 this->GetOutputInformation());
        
    vtkDataObject* curInput = inInfo->Get(vtkDataObject::DATA_OBJECT());
    if (curInput != prevInput)
      {
      inInfo->Remove(vtkDataObject::DATA_OBJECT());
      inInfo->Set(vtkDataObject::DATA_OBJECT(), prevInput);
      }
    vtkDataObject* curOutput = outInfo->Get(vtkDataObject::DATA_OBJECT());
    if (curOutput != compositeOutput)
      {
      compositeOutput->SetPipelineInformation(outInfo);
      }
    }
  this->ExecuteDataEnd(request,inInfoVec,outInfoVec);
}

//----------------------------------------------------------------------------
int vtkCompositeDataPipeline::NeedToExecuteData(
  int outputPort,
  vtkInformationVector** inInfoVec,
  vtkInformationVector* outInfoVec)
{
  // Has the algorithm asked to be executed again?
  if(this->ContinueExecuting)
    {
    return 1;
    }

  // If no port is specified, check all ports.  This behavior is
  // implemented by the superclass.
  if(outputPort < 0)
    {
    return this->Superclass::NeedToExecuteData(outputPort,
                                               inInfoVec,outInfoVec);
    }

  // Does the superclass want to execute?
  if(this->vtkDemandDrivenPipeline::NeedToExecuteData(
       outputPort,inInfoVec,outInfoVec))
    {
    return 1;
    }

  // We need to check the requested update extent.  Get the output
  // port information and data information.  We do not need to check
  // existence of values because it has already been verified by
  // VerifyOutputInformation.
  vtkInformation* outInfo = outInfoVec->GetInformationObject(outputPort);
  vtkDataObject* dataObject = outInfo->Get(vtkDataObject::DATA_OBJECT());
  if (!vtkCompositeDataSet::SafeDownCast(dataObject))
    {
    return this->Superclass::NeedToExecuteData(outputPort,
                                               inInfoVec,outInfoVec);
    }
  vtkInformation* dataInfo = dataObject->GetInformation();

  // Check the unstructured extent.  If we do not have the requested
  // piece, we need to execute.
  int updateNumberOfPieces = outInfo->Get(UPDATE_NUMBER_OF_PIECES());
  int dataNumberOfPieces = dataInfo->Get(vtkDataObject::DATA_NUMBER_OF_PIECES());
  if(dataNumberOfPieces != updateNumberOfPieces)
    {
    return 1;
    }
  if (dataNumberOfPieces != 1)
    {
    int dataPiece = dataInfo->Get(vtkDataObject::DATA_PIECE_NUMBER());
    int updatePiece = outInfo->Get(UPDATE_PIECE_NUMBER());
    if (dataPiece != updatePiece)
      {
      return 1;
      }
    }

  // if we are requesting a particular update time index, check
  // if we have the desired time index
  if ( outInfo->Has(UPDATE_TIME_STEPS()) )
    {
    if (!dataInfo->Has(vtkDataObject::DATA_TIME_STEPS()))
      {
      return 1;
      }
    int dlength = dataInfo->Length(vtkDataObject::DATA_TIME_STEPS());
    int ulength = outInfo->Length(UPDATE_TIME_STEPS());
    if (dlength != ulength)
      {
      return 1;
      }
    int cnt = 0;
    double *dsteps = dataInfo->Get(vtkDataObject::DATA_TIME_STEPS());
    double *usteps = outInfo->Get(UPDATE_TIME_STEPS());
    for (;cnt < dlength; ++cnt)
      {
      if (dsteps[cnt] != usteps[cnt])
        {
        return 1;
        }
      }
    }

  // We do not need to execute.
  return 0;
}

//----------------------------------------------------------------------------
int vtkCompositeDataPipeline::ForwardUpstream(vtkInformation* request)
{
  return this->Superclass::ForwardUpstream(request);
}

//----------------------------------------------------------------------------
int vtkCompositeDataPipeline::ForwardUpstream(
  int i, int j, vtkInformation* request)
{
  // Do not forward upstream if input information is shared.
  if(this->SharedInputInformation)
    {
    return 1;
    }

  if (!this->Algorithm->ModifyRequest(request, BeforeForward))
    {
    return 0;
    }

  int result = 1;
  if(vtkExecutive* e = this->GetInputExecutive(i, j))
    {
    vtkAlgorithmOutput* input = this->Algorithm->GetInputConnection(i, j);
    int port = request->Get(FROM_OUTPUT_PORT());
    request->Set(FROM_OUTPUT_PORT(), input->GetIndex());
    if(!e->ProcessRequest(request,
                          e->GetInputInformation(),
                          e->GetOutputInformation()))
      {
      result = 0;
      }
    request->Set(FROM_OUTPUT_PORT(), port);
    }

  if (!this->Algorithm->ModifyRequest(request, AfterForward))
    {
    return 0;
    }

  return result;
}

//----------------------------------------------------------------------------
void vtkCompositeDataPipeline::CopyDefaultInformation(
  vtkInformation* request, int direction,
  vtkInformationVector** inInfoVec,
  vtkInformationVector* outInfoVec)
{
  int hasUpdateBlocks = 0;
  if(direction == vtkExecutive::RequestDownstream)
    {
    vtkInformationKey** keys = request->Get(vtkExecutive::KEYS_TO_COPY());
    if (keys)
      {
      int len = request->Length(vtkExecutive::KEYS_TO_COPY());
      for (int i=0; i<len; i++)
        {
        if (keys[i] == vtkCompositeDataPipeline::UPDATE_BLOCKS())
          {
          hasUpdateBlocks = 1;
          break;
          }
        }
      if (hasUpdateBlocks)
        {
        request->Remove(vtkExecutive::KEYS_TO_COPY(),
                        vtkCompositeDataPipeline::UPDATE_BLOCKS());
        }
      }
    }

  this->Superclass::CopyDefaultInformation(request, direction, 
                                           inInfoVec, outInfoVec);

  if(request->Has(REQUEST_UPDATE_EXTENT()))
    {
    // Find the port that has a data that we will iterator over.
    // If there is one, make sure that we use piece extent for
    // that port. Composite data pipeline works with piece extents
    // only.
    int compositePort;
    if (this->ShouldIterateOverInput(compositePort))
      {
      // Get the output port from which to copy the extent.
      int outputPort = -1;
      if(request->Has(FROM_OUTPUT_PORT()))
        {
        outputPort = request->Get(FROM_OUTPUT_PORT());
        }
      
      // Setup default information for the inputs.
      if(outInfoVec->GetNumberOfInformationObjects() > 0)
        {
        // Copy information from the output port that made the request.
        // Since VerifyOutputInformation has already been called we know
        // there is output information with a data object.
        vtkInformation* outInfo =
          outInfoVec->GetInformationObject((outputPort >= 0)? outputPort : 0);

        // Loop over all connections on this input port.
        int numInConnections = 
          inInfoVec[compositePort]->GetNumberOfInformationObjects();
        for (int j=0; j<numInConnections; j++)
          {
          // Get the pipeline information for this input connection.
          vtkInformation* inInfo = 
            inInfoVec[compositePort]->GetInformationObject(j);
            
          inInfo->CopyEntry(outInfo, UPDATE_PIECE_NUMBER());
          inInfo->CopyEntry(outInfo, UPDATE_NUMBER_OF_PIECES());
          inInfo->CopyEntry(outInfo, UPDATE_NUMBER_OF_GHOST_LEVELS());
          inInfo->CopyEntry(outInfo, UPDATE_EXTENT_INITIALIZED());
          }
        }
      }
    }

  if (hasUpdateBlocks)
    {
    request->Append(vtkExecutive::KEYS_TO_COPY(), 
                    vtkCompositeDataPipeline::UPDATE_BLOCKS());
    }
}

//----------------------------------------------------------------------------
void vtkCompositeDataPipeline::CopyFromDataToInformation(
  vtkDataObject* dobj, vtkInformation* inInfo)
{
  if (dobj->IsA("vtkImageData"))
    {
    inInfo->Set(
      WHOLE_EXTENT(), static_cast<vtkImageData*>(dobj)->GetExtent(), 6);
    }
  else if (dobj->IsA("vtkStructuredGrid"))
    {
    inInfo->Set(
      WHOLE_EXTENT(), static_cast<vtkStructuredGrid*>(dobj)->GetExtent(), 6);
    }
  else if (dobj->IsA("vtkRectilinearGrid"))
    {
    inInfo->Set(
      WHOLE_EXTENT(), static_cast<vtkRectilinearGrid*>(dobj)->GetExtent(), 6);
    }
  else if (dobj->IsA("vtkUniformGrid"))
    {
    inInfo->Set(
      WHOLE_EXTENT(), static_cast<vtkUniformGrid*>(dobj)->GetExtent(), 6);
    }
  else
    {
    inInfo->Set(MAXIMUM_NUMBER_OF_PIECES(), 1);
    }
}

//----------------------------------------------------------------------------
void vtkCompositeDataPipeline::PushInformation(vtkInformation* inInfo)
{
  this->InformationCache->CopyEntry(inInfo, WHOLE_EXTENT());
  this->InformationCache->CopyEntry(inInfo, MAXIMUM_NUMBER_OF_PIECES());
}

//----------------------------------------------------------------------------
void vtkCompositeDataPipeline::PopInformation(vtkInformation* inInfo)
{
  inInfo->CopyEntry(this->InformationCache, WHOLE_EXTENT());
  inInfo->CopyEntry(this->InformationCache, MAXIMUM_NUMBER_OF_PIECES());
}

//----------------------------------------------------------------------------
int vtkCompositeDataPipeline::CheckCompositeData(
  int port, vtkInformationVector* outInfoVec)
{
  // Check that the given output port has a valid data object.
  vtkInformation* outInfo = outInfoVec->GetInformationObject(port);
  vtkDataObject* data = outInfo->Get(vtkDataObject::DATA_OBJECT());
  vtkInformation* portInfo = this->Algorithm->GetOutputPortInformation(port);

  if (const char* dt = portInfo->Get(COMPOSITE_DATA_TYPE_NAME()))
    {
    if(!data || !data->IsA(dt))
      {
      // Try to create an instance of the correct type.
      data = this->NewDataObject(dt);
      data->SetPipelineInformation(outInfo);
      if(data)
        {
        data->FastDelete();
        }
      }
    if (!data)
      {
      // The algorithm has a bug and did not create the data object.
      vtkErrorMacro("Algorithm " << this->Algorithm->GetClassName() << "("
                    << this->Algorithm
                    << ") did not create output for port " << port
                    << " when asked by REQUEST_DATA_OBJECT and does not"
                    << " specify a concrete COMPOSITE_DATA_TYPE_NAME.");
      return 0;
      }
    return 1;
    }

  // If this is a simple filter but has composite input, create a composite
  // output.
  int compositePort;
  if (this->ShouldIterateOverInput(compositePort))
    {
    // This assumes that the first output of the filter is the one
    // that will have the composite data.
    vtkDataObject* doOutput = 
      outInfo->Get(vtkDataObject::DATA_OBJECT());
    vtkCompositeDataSet* output = vtkCompositeDataSet::SafeDownCast(doOutput);
    if (!output)
      {
      output = vtkMultiGroupDataSet::New();
      output->SetPipelineInformation(outInfo);
      output->Delete();
      }
    return 1;
    }
  // Otherwise, create a simple output
  else
    {
    return this->Superclass::CheckDataObject(port, outInfoVec);
    }
}

//----------------------------------------------------------------------------
vtkDataObject* vtkCompositeDataPipeline::GetCompositeInputData(
  int port, int index, vtkInformationVector **inInfoVec)
{
  if (!inInfoVec[port])
    {
    return 0;
    }
  vtkInformation *info = inInfoVec[port]->GetInformationObject(index);
  if (!info)
    {
    return 0;
    }
  return info->Get(vtkDataObject::DATA_OBJECT());
}

//----------------------------------------------------------------------------
vtkDataObject* vtkCompositeDataPipeline::GetCompositeOutputData(int port)
{
  if(!this->OutputPortIndexInRange(port, "get data for"))
    {
    return 0;
    }

  // Check that the given output port has a valid data object.
  this->CheckCompositeData(port, this->GetOutputInformation());

  // Return the data object.
  if(vtkInformation* info = this->GetOutputInformation(port))
    {
    return info->Get(vtkDataObject::DATA_OBJECT());
    }
  return 0;
}

//----------------------------------------------------------------------------
void vtkCompositeDataPipeline::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

