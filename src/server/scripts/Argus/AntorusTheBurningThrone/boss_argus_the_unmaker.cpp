/*
* Copyright (C) 2008-2019 TrinityCore <http://www.trinitycore.org/>
*
* This program is free software; you can redistribute it and/or modify it
* under the terms of the GNU General Public License as published by the
* Free Software Foundation; either version 2 of the License, or (at your
* option) any later version.
*
* This program is distributed in the hope that it will be useful, but WITHOUT
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
* FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
* more details.
*
* You should have received a copy of the GNU General Public License along
* with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "GridNotifiers.h"
#include "ObjectMgr.h"
#include "AreaTriggerAI.h"
#include "PassiveAI.h"
#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "SpellAuras.h"
#include "SpellScript.h"
#include "SpellAuraEffects.h"
#include "SpellMgr.h"
#include "Vehicle.h"
#include "antorus_the_burning_throne.h"

enum Texts
{
    // Argus the Unmaker
    SAY_AGGRO                   = 0,
    SAY_DISENGAGE               = 1,

    // Aman'Thul
    SAY_SKY_AND_SEA             = 0,

    // Golganneth
    SAY_ANNOUNCE_SKY_AND_SEA    = 0
};

enum Spells
{
    // Argus the Unmaker
    SPELL_TITANIC_ESSENCE               = 258040,
    SPELL_ARGUS_P1_ENERGY_CONTROLLER    = 258041,
    SPELL_SWEEPING_SCYTHE               = 248499,
    SPELL_TORTURED_RAGE                 = 257296,
    SPELL_CONE_OF_DEATH                 = 256457,
    SPELL_TRIGGER_GOLGANNETH_ABILITY    = 256471,

    // Cone of Death
    SPELL_DEATH_FOG                     = 248167,
    SPELL_DEATH_FOG_WARN_VISUUAL        = 256570,

    // Golganneth
    SPELL_SKY_AND_SEA                   = 255594,
};

enum Events
{
    // Argus the Unmaker
    EVENT_SWEEPING_SCYTHE = 1,
    EVENT_TORTURED_RAGE,
    EVENT_CONE_OF_DEATH,
    EVENT_TRIGGER_GOLGANNETH_ABILITY,
};

enum Phases
{
    PHASE_ONE = 1,
};

enum SummonGroups
{
    SUMMON_GROUP_THE_PANTHEON = 0
};

struct boss_argus_the_unmaker : public BossAI
{
    boss_argus_the_unmaker(Creature* creature) : BossAI(creature, DATA_ARGUS_THE_UNMAKER)
    {
        Initialize();
    }

    void Initialize()
    {
        me->SetPowerType(POWER_ENERGY);
        me->SetMaxPower(POWER_ENERGY, 100);
        DoCastSelf(SPELL_TITANIC_ESSENCE); // we do it manually so the visual wont be removed by the power type change
    }

    void Reset() override
    {
        _Reset();
        Initialize();
        me->SummonCreatureGroup(SUMMON_GROUP_THE_PANTHEON);
    }

    void EnterCombat(Unit* /*who*/) override
    {
        _EnterCombat();
        Talk(SAY_AGGRO);
        DoCastSelf(SPELL_ARGUS_P1_ENERGY_CONTROLLER);
        instance->SendEncounterUnit(ENCOUNTER_FRAME_ENGAGE, me);
        events.SetPhase(PHASE_ONE);
        events.ScheduleEvent(EVENT_SWEEPING_SCYTHE, 6s, 0, PHASE_ONE);
        events.ScheduleEvent(EVENT_TORTURED_RAGE, 13s + 400ms, 0, PHASE_ONE);
        events.ScheduleEvent(EVENT_CONE_OF_DEATH, 1s, 0, PHASE_ONE);
        events.ScheduleEvent(EVENT_TRIGGER_GOLGANNETH_ABILITY, 12s, 0, PHASE_ONE);
    }

    void EnterEvadeMode(EvadeReason /*why*/) override
    {
        Talk(SAY_DISENGAGE);
        _EnterEvadeMode();
        instance->SendEncounterUnit(ENCOUNTER_FRAME_DISENGAGE, me);
        for (ObjectGuid guid : _coneOfDeathGUIDs)
        {
            if (AreaTrigger* cone = ObjectAccessor::GetAreaTrigger(*me, guid))
                cone->Remove();
        }

        events.Reset();
        summons.DespawnAll();
        _DespawnAtEvade();
    }

    void JustDied(Unit* /*killer*/) override
    {
        _JustDied();
        summons.DespawnAll();
        instance->SendEncounterUnit(ENCOUNTER_FRAME_DISENGAGE, me);
    }

    void JustRegisteredAreaTrigger(AreaTrigger* at) override
    {
        switch (at->GetEntry())
        {
            case AT_CONE_OF_DEATH:
                _coneOfDeathGUIDs.push_back(at->GetGUID());
                break;
            default:
                break;
        }
    }

    void JustUnregisteredAreaTrigger(AreaTrigger* at) override
    {
    }

    void SpellHit(Unit* /*caster*/, SpellInfo const* spell) override
    {
        switch (spell->Id)
        {
            case SPELL_TRIGGER_GOLGANNETH_ABILITY:
                if (Creature* amanthul = instance->GetCreature(DATA_AMANTHUL))
                    if (amanthul->IsAIEnabled)
                        amanthul->AI()->Talk(SAY_SKY_AND_SEA);
                break;
            default:
                break;
        }
    }

    void DoAction(int32 action) override
    {
    }

    void PassengerBoarded(Unit* passenger, int8 /*seatId*/, bool apply) override
    {
    }

    uint32 GetData(uint32 type) const override
    {
        return 0;
    }

    void DamageTaken(Unit* /*attacker*/, uint32& damage) override
    {
    }

    void MovementInform(uint32 type, uint32 id) override
    {
        if (type != POINT_MOTION_TYPE && type != EFFECT_MOTION_TYPE)
            return;
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        events.Update(diff);

        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_SWEEPING_SCYTHE:
                    DoCastVictim(SPELL_SWEEPING_SCYTHE);
                    events.Repeat(7s, 9s);
                    break;
                case EVENT_TORTURED_RAGE:
                    DoCastAOE(SPELL_TORTURED_RAGE);
                    events.Repeat(16s);
                    break;
                case EVENT_CONE_OF_DEATH:
                    if (me->GetPower(POWER_ENERGY) == 100)
                        DoCastSelf(SPELL_CONE_OF_DEATH);
                   events.Repeat(1s);
                    break;
                case EVENT_TRIGGER_GOLGANNETH_ABILITY:
                    DoCastSelf(SPELL_TRIGGER_GOLGANNETH_ABILITY);
                    events.Repeat(30s);
                    break;
                default:
                    break;
            }
        }
        DoMeleeAttackIfReady();
    }
private:
    GuidVector _coneOfDeathGUIDs;
};

class spell_argus_argus_p1_energy_controller : public AuraScript
{
    PrepareAuraScript(spell_argus_argus_p1_energy_controller);

    void HandlePeriodic(AuraEffect const* aurEff)
    {
        GetTarget()->ModifyPower(POWER_ENERGY, aurEff->GetAmount());
    }

    void Register() override
    {
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_argus_argus_p1_energy_controller::HandlePeriodic, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY);
    }
};

class spell_argus_cone_of_death : public SpellScript
{
    PrepareSpellScript(spell_argus_cone_of_death);

    void FilterTargets(std::list<WorldObject*>& targets)
    {
        if (targets.empty())
            return;

        Trinity::Containers::RandomResize(targets, 1);
    }

    void HandleHit(SpellEffIndex effIndex)
    {
        if (Unit* caster = GetCaster())
            caster->CastSpell(GetHitUnit(), GetSpellInfo()->GetEffect(effIndex)->BasePoints);
    }

    void Register() override
    {
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_argus_cone_of_death::FilterTargets, EFFECT_0, TARGET_UNIT_DEST_AREA_ENEMY);
        OnEffectHitTarget += SpellEffectFn(spell_argus_cone_of_death::HandleHit, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class spell_argus_trigger_golganneth_ability : public SpellScript
{
    PrepareSpellScript(spell_argus_trigger_golganneth_ability);

    void SetTarget(WorldObject*& target)
    {
        if (InstanceScript* instance = GetCaster()->GetInstanceScript())
            if (Creature* golganneth = instance->GetCreature(DATA_GOLGANNETH))
                target = golganneth;
    }

    void HandleScriptEffect(SpellEffIndex /*effIndex*/)
    {
        Creature* target = GetHitCreature();
        if (!target)
            return;

        if (target->IsAIEnabled)
            target->AI()->Talk(SAY_ANNOUNCE_SKY_AND_SEA);

        target->CastSpell(target, SPELL_SKY_AND_SEA);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_argus_trigger_golganneth_ability::HandleScriptEffect, EFFECT_1, SPELL_EFFECT_SCRIPT_EFFECT);
        OnObjectTargetSelect += SpellObjectTargetSelectFn(spell_argus_trigger_golganneth_ability::SetTarget, EFFECT_1, TARGET_UNIT_NEARBY_ENTRY);
    }
};


struct at_argus_cone_of_death : AreaTriggerAI
{
    at_argus_cone_of_death(AreaTrigger* areatrigger) : AreaTriggerAI(areatrigger) { }

    void OnUnitEnter(Unit* unit) override
    {
        if (!unit->IsPlayer())
            return;

        if (Unit* caster = at->GetCaster())
        {
            if (!unit->HasAura(SPELL_DEATH_FOG))
                unit->CastSpell(unit, SPELL_DEATH_FOG, true);

            if (!unit->HasAura(SPELL_DEATH_FOG_WARN_VISUUAL))
                unit->CastSpell(unit, SPELL_DEATH_FOG_WARN_VISUUAL, true);
        }
    }

    void OnUnitExit(Unit* unit) override
    {
        if (!unit->IsPlayer())
            return;

        unit->RemoveAurasDueToSpell(SPELL_DEATH_FOG);
        unit->RemoveAurasDueToSpell(SPELL_DEATH_FOG_WARN_VISUUAL);
    }
};

void AddSC_boss_argus_the_unmaker()
{
    RegisterAntorusTheBurningThroneCreatureAI(boss_argus_the_unmaker);
    RegisterAuraScript(spell_argus_argus_p1_energy_controller);
    RegisterSpellScript(spell_argus_cone_of_death);
    RegisterSpellScript(spell_argus_trigger_golganneth_ability);
    RegisterAreaTriggerAI(at_argus_cone_of_death);
}
