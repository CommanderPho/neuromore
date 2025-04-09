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
	LogDebug("ViewPlugin: Constructor called.");
	LogDetailedInfo("Constructing Signal View plugin ...");
	mViewWidget			= NULL;
}


// destructor
ViewPlugin::~ViewPlugin()
{
	LogDebug("ViewPlugin: Destructor called.");
	LogDetailedInfo("Destructing Signal View plugin ...");
	CORE_EVENTMANAGER.RemoveEventHandler(this);
}


// init after the parent dock window has been created
bool ViewPlugin::Init()
{
	LogDebug("ViewPlugin: Init() called.");
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

	LogDebug("ViewPlugin: Init() completed successfully.");
	return true;
}


// register attributes and create the default values
void ViewPlugin::RegisterAttributes()
{
	LogDebug("ViewPlugin: RegisterAttributes() called.");
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
	LogDebug("ViewPlugin: RegisterAttributes() completed.");
}


void ViewPlugin::OnAttributeChanged(Property* property)
{
	LogDebug("ViewPlugin: OnAttributeChanged() called for property '%s'.", property->GetAttributeSettings()->GetInternalName());
	const String& propertyInternalName = property->GetAttributeSettings()->GetInternalNameString();

	// timerange slider has changed
	if (propertyInternalName.IsEqual("timeRange") == true)
	{
		const double timerange = property->AsFloat();
		SetViewDuration(timerange);
	}
	LogDebug("ViewPlugin: OnAttributeChanged() completed.");
}


uint32 ViewPlugin::GetNumMultiChannels()
{
	LogDebug("ViewPlugin: GetNumMultiChannels() called.");
	Classifier* classifier = GetEngine()->GetActiveClassifier();
	if (classifier == NULL)
	{
		LogDebug("ViewPlugin: GetNumMultiChannels() completed. - classifier is NULL.");
		return 0;
	}

	uint32 numMultiChannels = classifier->GetNumViewMultiChannels();
	LogDebug("ViewPlugin: GetNumMultiChannels() completed.");
	return numMultiChannels;
}


const MultiChannel& ViewPlugin::GetMultiChannel(uint32 index)
{
	LogDebug("ViewPlugin: GetMultiChannel() called for index %u.", index);
	Classifier* classifier = GetEngine()->GetActiveClassifier();
	CORE_ASSERT(classifier);

	const MultiChannel& multiChannel = classifier->GetViewMultiChannel(index);
	LogDebug("ViewPlugin: GetMultiChannel() completed.");
	return multiChannel;
}


Core::Color ViewPlugin::GetChannelColor(uint32 multichannel, uint32 index)
{
	LogDebug("ViewPlugin: GetChannelColor() called for multichannel %u, index %u.", multichannel, index);
	Classifier* classifier = GetEngine()->GetActiveClassifier();
	CORE_ASSERT(classifier);

	const ViewNode& node = classifier->GetViewNodeForMultiChannel(multichannel);
	if (node.CustomColor() == true)
	{
		Core::Color customColor = node.GetCustomColor();
		LogDebug("ViewPlugin: GetChannelColor() completed.");
		return customColor;
	}
	
	Core::Color channelColor = classifier->GetViewMultiChannel(multichannel).GetChannel(index)->GetColor();
	LogDebug("ViewPlugin: GetChannelColor() completed.");
	return channelColor;
}


void ViewPlugin::SetViewDuration(double seconds)
{
	LogDebug("ViewPlugin: SetViewDuration() called with duration %f seconds.", seconds);
	Classifier* classifier = GetEngine()->GetActiveClassifier();
	if (classifier == NULL)
	{
		LogDebug("ViewPlugin: SetViewDuration() completed. - classifier is NULL.");
		return;
	}

	// set view duration of all view nodes in the classifier (always, even if the view mode is different)
	const uint32 numViewNodes = classifier->GetNumViewNodes();
	for (uint32 i=0; i<numViewNodes; ++i)
		classifier->GetViewNode(i)->SetViewDuration(seconds);
	LogDebug("ViewPlugin: SetViewDuration() completed.");
}

double ViewPlugin::GetFixedLength()
{
	LogDebug("ViewPlugin: GetFixedLength() called.");
	Classifier* classifier = GetEngine()->GetActiveClassifier();
	CORE_ASSERT(classifier);

	const uint32 numViewNodes = classifier->GetNumViewNodes();
	for (uint32 i=0; i<numViewNodes; ++i)
	{
		double fixedLength = classifier->GetViewNode(i)->GetFixedLength();
		if (fixedLength > 0.)
		{
			LogDebug("ViewPlugin: GetFixedLength() completed.");
			return fixedLength;
		}
	}
	LogDebug("ViewPlugin: GetFixedLength() completed.");
	return -1.;
}

