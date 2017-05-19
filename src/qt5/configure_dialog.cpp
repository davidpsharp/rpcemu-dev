/*
  RPCEmu - An Acorn system emulator

  Copyright (C) 2016-2017 Matthew Howkins

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <iostream>

#include "configure_dialog.h"

ConfigureDialog::ConfigureDialog(Config *config_copy, Model *model_copy, QWidget *parent)
    : QDialog(parent)
{
	setWindowTitle("Configure RPCEmu");

	// Store pointer to the GUI thread's copy of the emulator config
	this->config_copy = config_copy;
	this->model_copy = model_copy;

	// Create actions

	// Create widgets and layout

	// Create Hardware Group
	hardware_listwidget = new QListWidget();

	// Fill in ListWidget with models
	int modeliter =  0;
	while(modeliter < (int) Model_MAX) {
		QListWidgetItem *item = new QListWidgetItem(models[modeliter].name_gui);
		hardware_list_items.insert(hardware_list_items.end(), item);
		hardware_listwidget->addItem(item);
		modeliter++;
	}

	hardware_vbox = new QVBoxLayout();
	hardware_vbox->addWidget(hardware_listwidget);

	hardware_group_box = new QGroupBox("Hardware");
	hardware_group_box->setLayout(hardware_vbox);

	// Create Memory Group
	mem_4 = new QRadioButton("4 MB");
	mem_8 = new QRadioButton("8 MB");
	mem_16 = new QRadioButton("16 MB");
	mem_32 = new QRadioButton("32 MB (recommended)");
	mem_64 = new QRadioButton("64 MB (recommended)");
	mem_128 = new QRadioButton("128 MB (recommended)");
	mem_256 = new QRadioButton("256 MB");

	mem_group = new QButtonGroup();
	mem_group->addButton(mem_4);
	mem_group->addButton(mem_8);
	mem_group->addButton(mem_16);
	mem_group->addButton(mem_32);
	mem_group->addButton(mem_64);
	mem_group->addButton(mem_128);
	mem_group->addButton(mem_256);

	mem_vbox = new QVBoxLayout();
	mem_vbox->addWidget(mem_4);
	mem_vbox->addWidget(mem_8);
	mem_vbox->addWidget(mem_16);
	mem_vbox->addWidget(mem_32);
	mem_vbox->addWidget(mem_64);
	mem_vbox->addWidget(mem_128);
	mem_vbox->addWidget(mem_256);

	mem_group_box = new QGroupBox("RAM");
	mem_group_box->setLayout(mem_vbox);

	// Create VRAM group
	vram_0 = new QRadioButton("None");
	vram_2 = new QRadioButton("2 MB (8 MB if OS supported)");

	vram_group = new QButtonGroup();
	vram_group->addButton(vram_0);
	vram_group->addButton(vram_2);

	vram_vbox = new QVBoxLayout();
	vram_vbox->addWidget(vram_0);
	vram_vbox->addWidget(vram_2);

	vram_group_box = new QGroupBox("VRAM");
	vram_group_box->setLayout(vram_vbox);

	// Create sound checkbox
	sound_checkbox = new QCheckBox("Sound");

	// Create refresh
	refresh_slider = new QSlider(Qt::Horizontal);
	refresh_slider->setRange(20, 100);
	refresh_slider->setTickPosition(QSlider::TicksBothSides);
	refresh_slider->setFixedWidth(256);

	refresh_label = new QLabel("");

	refresh_hbox = new QHBoxLayout();
	refresh_hbox->addWidget(refresh_slider);
	refresh_hbox->addWidget(refresh_label);

	refresh_group_box = new QGroupBox("Video refresh rate");
	refresh_group_box->setLayout(refresh_hbox);

	// Create Buttons
	buttons_box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

	// Main layout
	grid = new QGridLayout(this);
	grid->addWidget(hardware_group_box, 0, 0);
	grid->addWidget(mem_group_box, 0, 1);
	grid->addWidget(vram_group_box, 1, 0);
	grid->addWidget(sound_checkbox, 1, 1);
	grid->addWidget(refresh_group_box, 2, 0, 1, 2); // span 2 columns
	grid->addWidget(buttons_box, 3, 0, 1, 2);       // span 2 columns

	// Connect actions to widgets
	connect(refresh_slider, SIGNAL(sliderMoved(int)), this, SLOT(slider_moved(int)));

	connect(buttons_box, SIGNAL(accepted()), this, SLOT(accept()));
	connect(buttons_box, SIGNAL(rejected()), this, SLOT(reject()));

	connect(this, SIGNAL(accepted()), this, SLOT(dialog_accepted()));
	connect(this, SIGNAL(rejected()), this, SLOT(dialog_rejected()));

	// Set the values in the configure dialog box
	applyConfig();
}


ConfigureDialog::~ConfigureDialog()
{
}

void
ConfigureDialog::keyPressEvent(QKeyEvent *evt)
{
	std::cout << "keyPressEvent()" << std::endl;

	if (evt->key() == Qt::Key_Enter || evt->key() == Qt::Key_Return) {
		return;
	}
	QDialog::keyPressEvent(evt);
}

void
ConfigureDialog::slider_moved(int value)
{
	refresh_label->setText(QString::number(value) + " Hz");
}

/**
 * User clicked OK on the Configure dialog box 
 */
void
ConfigureDialog::dialog_accepted()
{
	std::cout << "dialog_accepted()" << std::endl;
}

/**
 * User clicked cancel on the Configure dialog box 
 */
void
ConfigureDialog::dialog_rejected()
{
	// Set the values in the dialog back to the current settings
	applyConfig();
}

/**
 * Set the values in the configure dialog box based on the current
 * values of the GUI config copy
 */
void
ConfigureDialog::applyConfig()
{
	// Hardware Model
	hardware_listwidget->setCurrentItem(hardware_list_items.at((int) *model_copy));

	// RAM Size
	mem_4->setChecked(false);
	mem_8->setChecked(false);
	mem_16->setChecked(false);
	mem_32->setChecked(false);
	mem_64->setChecked(false);
	mem_128->setChecked(false);
	mem_256->setChecked(false);

	switch(config_copy->mem_size) {
	case   4: mem_4->setChecked(true);   break;
	case   8: mem_8->setChecked(true);   break;
	case  16: mem_16->setChecked(true);  break;
	case  32: mem_32->setChecked(true);  break;
	case  64: mem_64->setChecked(true);  break;
	case 128: mem_128->setChecked(true); break;
	case 256: mem_256->setChecked(true); break;
	default: fatal("configuredialog.cpp: unhandled memsize %u", config_copy->mem_size);
	}

	// VRAM
	vram_0->setChecked(false);
	vram_2->setChecked(false);

	switch(config_copy->vrammask) {
	case        0: vram_0->setChecked(true); break;
	case 0x7FFFFF: vram_2->setChecked(true); break;
	// TODO Pheobe? cos it'll trip this defualt ...
	default: fatal("configuredialog.cpp: unhandled vram size 0x%08x", config_copy->vrammask);
	}

	// Sound
	if(config_copy->soundenabled) {
		sound_checkbox->setChecked(true);
	} else {
		sound_checkbox->setChecked(false);
	}

	// Video Refresh Rate
	refresh_slider->setValue(config_copy->refresh);
	refresh_label->setText(QString::number(config_copy->refresh) + " Hz");
}
