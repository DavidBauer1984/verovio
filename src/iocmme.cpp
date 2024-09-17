/////////////////////////////////////////////////////////////////////////////
// Name:        iocmme.cpp
// Author:      Laurent Pugin
// Created:     2024
// Copyright (c) Authors and others. All rights reserved.
/////////////////////////////////////////////////////////////////////////////

#include "iocmme.h"

//----------------------------------------------------------------------------

#include <cassert>
#include <fstream>
#include <sstream>
#include <string>

//----------------------------------------------------------------------------

#include "barline.h"
#include "clef.h"
#include "doc.h"
#include "dot.h"
#include "layer.h"
#include "mdiv.h"
#include "measure.h"
#include "mensur.h"
#include "note.h"
#include "rest.h"
#include "score.h"
#include "section.h"
#include "staff.h"
#include "staffdef.h"
#include "staffgrp.h"
#include "vrv.h"

//----------------------------------------------------------------------------

namespace vrv {

//----------------------------------------------------------------------------
// CmmeInput
//----------------------------------------------------------------------------

CmmeInput::CmmeInput(Doc *doc) : Input(doc)
{
    m_score = NULL;
    m_currentSection = NULL;
    m_currentLayer = NULL;
    m_currentMensuration = NULL;
}

CmmeInput::~CmmeInput() {}

//////////////////////////////////////////////////////////////////////////

bool CmmeInput::Import(const std::string &cmme)
{
    try {
        m_doc->Reset();
        m_doc->SetType(Raw);
        pugi::xml_document doc;
        doc.load_string(cmme.c_str(), (pugi::parse_comments | pugi::parse_default) & ~pugi::parse_eol);
        pugi::xml_node root = doc.first_child();

        // The mDiv
        Mdiv *mdiv = new Mdiv();
        mdiv->m_visibility = Visible;
        m_doc->AddChild(mdiv);
        // The score
        m_score = new Score();
        mdiv->AddChild(m_score);

        pugi::xpath_node_set voices = root.select_nodes("/Piece/VoiceData/Voice");

        for (pugi::xpath_node voiceNode : voices) {
            m_numVoices++;
            std::string name = ChildAsString(voiceNode.node(), "Name");
            m_voices.push_back(name);
        }
        m_mensurations.resize(m_numVoices);

        pugi::xpath_node_set musicSections = root.select_nodes("/Piece/MusicSection/*");

        for (pugi::xpath_node musicSectionNode : musicSections) {
            MakeSection(musicSectionNode.node());
        }

        // add minimal scoreDef
        StaffGrp *staffGrp = new StaffGrp();
        for (int i = 0; i < m_numVoices; ++i) {
            StaffDef *staffDef = new StaffDef();
            staffDef->SetN(i + 1);
            staffDef->SetLines(5);
            staffDef->SetNotationtype(NOTATIONTYPE_mensural);
            staffGrp->AddChild(staffDef);
            // Default mensur with everything binary in CMME
            Mensur *mensur = new Mensur();
            mensur->SetProlatio(PROLATIO_2);
            mensur->SetTempus(TEMPUS_2);
            mensur->SetModusminor(MODUSMINOR_2);
            mensur->SetModusmaior(MODUSMAIOR_2);
            staffDef->AddChild(mensur);
        }

        m_score->GetScoreDef()->AddChild(staffGrp);

        m_doc->ConvertToPageBasedDoc();
        // return this->ReadDoc(root);
    }
    catch (char *str) {
        LogError("%s", str);
        return false;
    }

    return true;
}

void CmmeInput::MakeSection(pugi::xml_node musicSectionNode)
{
    assert(m_score);

    std::string sectionType = musicSectionNode.name();

    // the section
    Section *section = new Section();
    section->SetType(sectionType);
    m_score->AddChild(section);

    m_currentSection = new Measure(UNMEASURED, 1);
    section->AddChild(m_currentSection);

    pugi::xpath_node_set voices = musicSectionNode.select_nodes("./Voice");

    for (int i = 0; i < m_numVoices; ++i) {
        std::string xpath = StringFormat("./Voice[VoiceNum[text()='%d']]", i + 1);
        pugi::xpath_node voice = musicSectionNode.select_node(xpath.c_str());
        if (voice) {
            MakeStaff(voice.node());
        }
        else {
            Staff *staff = new Staff(i + 1);
            staff->SetVisible(BOOLEAN_false);
            m_currentSection->AddChild(staff);
        }
    }
}

void CmmeInput::MakeStaff(pugi::xml_node voiceNode)
{
    assert(m_currentSection);

    int numVoice = this->ChildAsInt(voiceNode, "VoiceNum");

    Staff *staff = new Staff(numVoice);
    m_currentLayer = new Layer();
    m_currentLayer->SetN(1);

    m_currentMensuration = &m_mensurations.at(numVoice - 1);

    pugi::xpath_node_set events = voiceNode.select_nodes("./EventList/*");

    for (pugi::xpath_node event : events) {
        pugi::xml_node eventNode = event.node();
        std::string name = eventNode.name();
        if (name == "Clef") {
            if (eventNode.select_node("./Signature")) {
            }
            else {
                MakeClef(eventNode);
            }
        }
        else if (name == "Dot") {
            MakeDot(eventNode);
        }
        else if (name == "Mensuration") {
            MakeMensuration(eventNode);
        }
        else if (name == "Note") {
            MakeNote(eventNode);
        }
        else if (name == "OriginalText") {
            MakeOriginalText(eventNode);
        }
        else if (name == "Rest") {
            MakeRest(eventNode);
        }
        else {
            LogWarning("Unsupported event '%s'", name.c_str());
        }
    }

    staff->AddChild(m_currentLayer);
    m_currentSection->AddChild(staff);
}

void CmmeInput::MakeClef(pugi::xml_node clefNode)
{
    static const std::map<std::string, data_CLEFSHAPE> shapeMap{
        { "C", CLEFSHAPE_C }, //
        { "F", CLEFSHAPE_F }, //
        { "G", CLEFSHAPE_G }, //
        { "Frnd", CLEFSHAPE_F }, //
    };

    std::string appearance = this->ChildAsString(clefNode, "Appearance");
    if (!shapeMap.contains(appearance)) {
        LogWarning("Unknown clef '%s", appearance.c_str());
        return;
    }

    Clef *clef = new Clef();
    int staffLoc = this->ChildAsInt(clefNode, "StaffLoc");
    staffLoc = (staffLoc + 1) / 2;
    clef->SetLine(staffLoc);

    data_CLEFSHAPE shape = shapeMap.at(appearance);
    clef->SetShape(shape);

    m_currentLayer->AddChild(clef);

    return;
}

void CmmeInput::MakeDot(pugi::xml_node dotNode)
{
    Dot *dot = new Dot();
    m_currentLayer->AddChild(dot);

    return;
}

void CmmeInput::MakeMensuration(pugi::xml_node mensurationNode)
{
    pugi::xml_node mensInfo = mensurationNode.child("MensInfo");
    if (mensInfo) {
        m_currentMensuration->prolatio = this->ChildAsInt(mensInfo, "Prolatio");
        m_currentMensuration->tempus = this->ChildAsInt(mensInfo, "Tempus");
        m_currentMensuration->modusminor = this->ChildAsInt(mensInfo, "ModusMinor");
        m_currentMensuration->modusmaior = this->ChildAsInt(mensInfo, "ModusMaior");
    }

    Mensur *mensur = new Mensur();
    data_PROLATIO prolatio = (m_currentMensuration->prolatio == 3) ? PROLATIO_3 : PROLATIO_2;
    mensur->SetProlatio(prolatio);
    data_TEMPUS tempus = (m_currentMensuration->tempus == 3) ? TEMPUS_3 : TEMPUS_2;
    mensur->SetTempus(tempus);
    data_MODUSMINOR modusminor = (m_currentMensuration->modusminor == 3) ? MODUSMINOR_3 : MODUSMINOR_2;
    mensur->SetModusminor(modusminor);
    data_MODUSMAIOR modusmaior = (m_currentMensuration->modusmaior == 3) ? MODUSMAIOR_3 : MODUSMAIOR_2;
    mensur->SetModusmaior(modusmaior);
    data_MENSURATIONSIGN sign = (m_currentMensuration->tempus == 3) ? MENSURATIONSIGN_O : MENSURATIONSIGN_C;
    mensur->SetSign(sign);
    data_BOOLEAN dot = (m_currentMensuration->prolatio == 3) ? BOOLEAN_true : BOOLEAN_false;
    mensur->SetDot(dot);

    m_currentLayer->AddChild(mensur);

    return;
}

void CmmeInput::MakeNote(pugi::xml_node noteNode)
{
    static const std::map<std::string, data_PITCHNAME> Step2PitchName{
        { "C", PITCHNAME_c }, //
        { "D", PITCHNAME_d }, //
        { "E", PITCHNAME_e }, //
        { "F", PITCHNAME_f }, //
        { "G", PITCHNAME_g }, //
        { "A", PITCHNAME_a }, //
        { "B", PITCHNAME_b } //
    };

    Note *note = new Note();
    std::string step = this->ChildAsString(noteNode, "LetterName");
    data_PITCHNAME pname = Step2PitchName.contains(step) ? Step2PitchName.at(step) : PITCHNAME_c;
    note->SetPname(pname);

    int num;
    int numbase;
    data_DURATION duration = this->ReadDuration(noteNode, num, numbase);
    note->SetDur(duration);
    if (num != VRV_UNSET && numbase != VRV_UNSET) {
        note->SetNumbase(num);
        note->SetNum(numbase);
    }

    int oct = this->ChildAsInt(noteNode, "OctaveNum");
    if ((pname != PITCHNAME_a) && (pname != PITCHNAME_b)) oct += 1;
    note->SetOct(oct);

    if (noteNode.child("Colored")) {
        note->SetColored(BOOLEAN_true);
    }

    m_currentLayer->AddChild(note);

    return;
}

void CmmeInput::MakeOriginalText(pugi::xml_node originalTextNode)
{
    return;
}

void CmmeInput::MakeRest(pugi::xml_node restNode)
{
    Rest *rest = new Rest();
    int num;
    int numbase;
    data_DURATION duration = this->ReadDuration(restNode, num, numbase);
    rest->SetDur(duration);
    if (num != VRV_UNSET && numbase != VRV_UNSET) {
        rest->SetNumbase(num);
        rest->SetNum(numbase);
    }

    m_currentLayer->AddChild(rest);

    return;
}

data_DURATION CmmeInput::ReadDuration(pugi::xml_node durationNode, int &num, int &numbase)
{
    static const std::map<std::string, data_DURATION> durationMap{
        { "Maxima", DURATION_maxima }, //
        { "Longa", DURATION_longa }, //
        { "Brevis", DURATION_brevis }, //
        { "Semibrevis", DURATION_semibrevis }, //
        { "Minima", DURATION_minima }, //
        { "Semiminima", DURATION_semiminima }, //
        { "Fusa", DURATION_fusa }, //
        { "Semifusa", DURATION_semifusa } //
    };

    std::string type = this->ChildAsString(durationNode, "Type");
    data_DURATION duration = durationMap.contains(type) ? durationMap.at(type) : DURATION_brevis;

    num = VRV_UNSET;
    numbase = VRV_UNSET;

    if (durationNode.child("Length")) {
        num = this->ChildAsInt(durationNode.child("Length"), "Num");
        numbase = this->ChildAsInt(durationNode.child("Length"), "Den");

        std::pair<int, int> ratio = { 1, 1 };

        if (type == "Maxima") {
            ratio.first *= m_currentMensuration->modusmaior * m_currentMensuration->modusminor
                * m_currentMensuration->tempus * m_currentMensuration->prolatio;
        }
        else if (type == "Longa") {
            ratio.first
                *= m_currentMensuration->modusminor * m_currentMensuration->tempus * m_currentMensuration->prolatio;
        }
        else if (type == "Brevis") {
            ratio.first *= m_currentMensuration->tempus * m_currentMensuration->prolatio;
        }
        else if (type == "Semibrevis") {
            ratio.first *= m_currentMensuration->prolatio;
        }
        else if (type == "Semiminima") {
            ratio.second = 2;
        }
        else if (type == "Fusa") {
            ratio.second = 4;
        }
        else if (type == "Semifusa") {
            ratio.second = 8;
        }

        if (ratio.first != num || ratio.second != numbase) {
            num *= ratio.second;
            numbase *= ratio.first;
        }
        else {
            num = VRV_UNSET;
            numbase = VRV_UNSET;
        }
    }

    return duration;
}

std::string CmmeInput::AsString(const pugi::xml_node node) const
{
    assert(node);

    if (node.text()) {
        return std::string(node.text().as_string());
    }
    return "";
}

std::string CmmeInput::ChildAsString(const pugi::xml_node node, const std::string &child) const
{
    pugi::xpath_node childNode = node.select_node(child.c_str());
    if (childNode.node()) {
        return this->AsString(childNode.node());
    }
    return "";
}

int CmmeInput::AsInt(const pugi::xml_node node) const
{
    assert(node);

    if (node.text()) {
        return (node.text().as_int());
    }
    return VRV_UNSET;
}

int CmmeInput::ChildAsInt(const pugi::xml_node node, const std::string &child) const
{
    pugi::xpath_node childNode = node.select_node(child.c_str());
    if (childNode.node()) {
        return this->AsInt(childNode.node());
    }
    return VRV_UNSET;
}

} // namespace vrv
