/****************************************************************************
**
** Copyright 2019 neuromore co
** Contact: https://neuromore.com/contact
**
** Commercial License Usage
** Licensees holding valid commercial neuromore licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and neuromore. For licensing terms
** and conditions see https://neuromore.com/licensing. For further
** information use the contact form at https://neuromore.com/contact.
**
** neuromore Public License Usage
** Alternatively, this file may be used under the terms of the neuromore
** Public License version 1 as published by neuromore co with exceptions as 
** appearing in the file neuromore-class-exception.md included in the 
** packaging of this file. Please review the following information to 
** ensure the neuromore Public License requirements will be met: 
** https://neuromore.com/npl
**
****************************************************************************/

// include precompiled header
#include <Studio/Precompiled.h>

// include required headers
#include "ViewPlugin.h"
#include <DSP/Epoch.h>

using namespace Core;

// constructor
ViewPlugin::ViewPlugin() : Plugin(GetStaticTypeUuid())
{
	LogDetailedInfo("Constructing Signal View plugin ...");
	mViewWidget = nullptr;
}


// destructor
ViewPlugin::~ViewPlugin()
{
	LogDetailedInfo("Destructing Signal View plugin ...");

	// Safely remove event handler
	CORE_EVENTMANAGER.RemoveEventHandler(this);

	// Safely delete mViewWidget
	if (mViewWidget != nullptr)
	{
		delete mViewWidget;
		mViewWidget = nullptr;
	}
}


// init after the parent dock window has been created
bool ViewPlugin::Init()
{
	LogDetailedInfo("Initializing Signal View plugin ...");

	QWidget*		mainWidget		= NULL;
	QHBoxLayout*	mainLayout		= NULL;
	CreateDockMainWidget( &mainWidget, &mainLayout );

	///////////////////////////////////////////////////////////////////////////
	// Toolbar (top-left)
	///////////////////////////////////////////////////////////////////////////

	Core::Array<QWidget*> toolbarWidgets;

	///////////////////////////////////////////////////////////////////////////
	// Settings
	///////////////////////////////////////////////////////////////////////////

	// create the attribute set grid widget 
	AttributeSetGridWidget* attributeSetGridWidget = new AttributeSetGridWidget( GetDockWidget() );
	attributeSetGridWidget->ReInit(this);

	SetSettingsWidget( attributeSetGridWidget );

	///////////////////////////////////////////////////////////////////////////
	// Add render widget at the end
	///////////////////////////////////////////////////////////////////////////

	QWidget* vWidget = new QWidget(mainWidget);
	vWidget->hide();
	QVBoxLayout* vLayout = new QVBoxLayout();
	vLayout->setMargin(0);
	vLayout->setSpacing(0);
	vWidget->setLayout(vLayout);

	// add the view widget
	mViewWidget = new ViewWidget(this, vWidget);
	SetRealtimeWidget( mViewWidget );
	vLayout->addWidget( mViewWidget );
	UpdateInterface();
	
	///////////////////////////////////////////////////////////////////////////
	// Fill everything
	///////////////////////////////////////////////////////////////////////////
	FillLayouts(mainWidget, mainLayout, toolbarWidgets, "Settings", "Gear", vWidget);

	vWidget->show();
	
	CORE_EVENTMANAGER.AddEventHandler(this);

	LogDetailedInfo("Signal View plugin successfully initialized");

	connect(attributeSetGridWidget->GetPropertyManager(), SIGNAL(ValueChanged(Property*)), this, SLOT(OnAttributeChanged(Property*)));

	// Ensure mViewWidget is valid before using it
	if (mViewWidget == nullptr)
	{
		LogError("Failed to initialize ViewWidget.");
		return false;
	}

	// Limit the number of multi-channels processed to avoid excessive resource usage
	uint32 maxMultiChannels = 100; // Arbitrary limit for safety
	if (GetNumMultiChannels() > maxMultiChannels)
	{
		LogError("Too many multi-channels detected. Limiting to prevent application freeze.");
		return false;
	}

	return true;
}


// register attributes and create the default values
void ViewPlugin::RegisterAttributes()
{
	// register base class attributes
	Plugin::RegisterAttributes();

	// displayed interval duration
	AttributeSettings* attributeTimeRange = RegisterAttribute("Time Range (s)", "timeRange", "Length of the displayed interval in seconds.", ATTRIBUTE_INTERFACETYPE_FLOATSLIDER);
	attributeTimeRange->SetDefaultValue( AttributeFloat::Create(ViewNode::VIEWDURATION) );
	attributeTimeRange->SetMinValue( AttributeFloat::Create(1.0f) );
	attributeTimeRange->SetMaxValue( AttributeFloat::Create(ViewNode::VIEWDURATIONMAX) );

	// set default view duration
	SetViewDuration(ViewNode::VIEWDURATION);

	// visual sample style
	AttributeSettings* attributeStyle = RegisterAttribute("Style", "style", "The visual appearance of the chart.", ATTRIBUTE_INTERFACETYPE_COMBOBOX);
	attributeStyle->AddComboValue("Boxes");
	attributeStyle->AddComboValue("Bars");
	attributeStyle->AddComboValue("Lollipops");
	attributeStyle->AddComboValue("Dots");
	attributeStyle->AddComboValue("Lines");
	attributeStyle->SetDefaultValue( AttributeInt32::Create(4) );	// use lines as default style

	// show latency marker checkbox
	AttributeSettings* attributeShowLatencyMarker = RegisterAttribute("Show Latency", "showLatencyMarker", "Show a latency indicator marking the average latent sample.", ATTRIBUTE_INTERFACETYPE_CHECKBOX);
	attributeShowLatencyMarker->SetDefaultValue( AttributeBool::Create(false) );

	CreateDefaultAttributeValues();
}


void ViewPlugin::OnAttributeChanged(Property* property)
{
	const String& propertyInternalName = property->GetAttributeSettings()->GetInternalNameString();

	// timerange slider has changed
	if (propertyInternalName.IsEqual("timeRange") == true)
	{
		const double timerange = property->AsFloat();
		SetViewDuration(timerange);
	}
}


uint32 ViewPlugin::GetNumMultiChannels()
{
	Classifier* classifier = GetEngine()->GetActiveClassifier();
	if (classifier == nullptr)
		return 0;

	// Limit the number of multi-channels to prevent excessive processing
	uint32 numMultiChannels = classifier->GetNumViewMultiChannels();
	const uint32 maxAllowedChannels = 100; // Arbitrary safety limit
	if (numMultiChannels > maxAllowedChannels)
	{
		LogWarning("Number of multi-channels exceeds safety limit. Limiting to prevent freeze.");
		return maxAllowedChannels;
	}

	return numMultiChannels;
}


const MultiChannel& ViewPlugin::GetMultiChannel(uint32 index)
{
	Classifier* classifier = GetEngine()->GetActiveClassifier();
	if (classifier == nullptr || index >= classifier->GetNumViewMultiChannels())
	{
		static MultiChannel emptyChannel;
		return emptyChannel;
	}

	return classifier->GetViewMultiChannel(index);
}


Core::Color ViewPlugin::GetChannelColor(uint32 multichannel, uint32 index)
{
	Classifier* classifier = GetEngine()->GetActiveClassifier();
	if (classifier == nullptr || multichannel >= classifier->GetNumViewMultiChannels())
		return Core::Color(); // Return default color

	const ViewNode& node = classifier->GetViewNodeForMultiChannel(multichannel);
	if (node.CustomColor() == true)
		return node.GetCustomColor();

	const MultiChannel& multiChannel = classifier->GetViewMultiChannel(multichannel);
	if (index >= multiChannel.GetNumChannels())
		return Core::Color();

	return multiChannel.GetChannel(index)->GetColor();
}


void ViewPlugin::SetViewDuration(double seconds)
{
	Classifier* classifier = GetEngine()->GetActiveClassifier();
	if (classifier == nullptr)
		return;

	// Set a maximum allowed view duration to prevent excessive processing
	const double maxDuration = 3600.0; // 1 hour
	if (seconds > maxDuration)
	{
		LogWarning("Requested view duration exceeds maximum allowed. Clamping to safety limit.");
		seconds = maxDuration;
	}

	// set view duration of all view nodes in the classifier (always, even if the view mode is different)
	const uint32 numViewNodes = classifier->GetNumViewNodes();
	for (uint32 i=0; i<numViewNodes; ++i)
		classifier->GetViewNode(i)->SetViewDuration(seconds);
}

double ViewPlugin::GetFixedLength()
{
	Classifier* classifier = GetEngine()->GetActiveClassifier();
	if (classifier == nullptr)
		return -1.0;

	const uint32 numViewNodes = classifier->GetNumViewNodes();
	for (uint32 i = 0; i < numViewNodes; ++i)
	{
		double fixedLength = classifier->GetViewNode(i)->GetFixedLength();
		if (fixedLength > 0.0)
			return fixedLength;
	}
	return -1.0;
}

