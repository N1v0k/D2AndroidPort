/* ---- Las IA del original de los monstruos del Acto 1 (se incluye desde monai.inc) ----

   AITHINK_Fn002_Skeleton, Fn003_Zombie, Fn005_BloodHawk, Fn007_Brute,
   Fn009_Wraith, Fn010_CorruptRogue y Fn035_CorruptArcher de AiThink.cpp
   (D2MOO), con sus parámetros aip de monstats.txt (los de Normal). Las
   velocidades de AITACTICS_SetVelocity son un porcentaje que se suma
   (STAT_VELOCITYPERCENT): +100 es el doble. */

static int ai_think1(Unit *u, const Map *m, UnitTarget *pl, int t, float d, int hurt)
{
    (void)m;
    const UnitTarget *tp = t >= 0 ? &pl[t] : NULL;
    int combat = tp && d <= u->reach, life = u->max_hp > 0 ? u->hp * 100 / u->max_hp : 100;
    if (!tp)
        return 0;
    switch (u->ai) {
    case AI_SKELETON: /* aip: acercarse, quieto, pegar, A1 o A2 */
        if (combat) {
            if (ai_roll(u, 2)) {
                ai_attack12(u, t, 3);
                return 1;
            }
        } else if (ai_roll(u, 0)) {
            ai_go(u, tp->x, tp->y, 10, 100, 1);
            return 1;
        }
        ai_stall(u, u->aip[1]);
        return 1;
    case AI_ZOMBIE: /* aip: acercarse, distancia a la que se da cuenta, -, A1 o A2 */
        if (combat) {
            ai_attack12(u, t, 3);
            return 1;
        }
        /* si le pegaron, o lo vio cerca, o en el Burial Grounds (los de Blood Raven): corre (+100) */
        if (hurt || (d < u->aip[1] && ai_roll(u, 0)) || (s_level_id_at && s_level_id_at(u->x, u->y) == 17)) {
            ai_run_to(u, tp->x, tp->y, 10, 200, 1);
            return 1;
        }
        ai_wander(u, 3, 100);
        return 1;
    case AI_BLOODHAWK: /* aip: cargar, vagar, pegar, velocidad al huir, velocidad al cargar */
        if (u->ai_p[0] == 1 && combat) { /* llegó cargando: pica */
            u->ai_p[0] = 0;
            ai_attack(u, t, ANIM_ATTACK);
            return 1;
        }
        u->ai_p[0] = 0;
        if (combat) {
            if (!ai_roll(u, 2)) { /* casi siempre se va volando y vuelve */
                ai_escape(u, tp, 4.0f);
                u->ai_spd = 100 + u->aip[3];
                return 1;
            }
            ai_attack(u, t, ANIM_ATTACK);
            return 1;
        }
        if (ai_roll(u, 0)) {
            u->ai_p[0] = 1;
            ai_go(u, tp->x, tp->y, 20, 100 + u->aip[4], 1);
            return 1;
        }
        if (d <= 3.0f) {
            ai_escape(u, tp, 4.0f);
            u->ai_spd = 100 + u->aip[3];
            return 1;
        }
        if (!ai_roll(u, 1))
            ai_wander(u, 3, 100);
        else
            ai_wander(u, 4, 50); /* SetVelocity -50 */
        return 1;
    case AI_BRUTE: /* aip: -, rodear (sin usar), pegar, A1 o A2 */
        if (combat) {
            if (ai_roll(u, 2))
                ai_attack12(u, t, 3);
            else if (ai_roll(u, 2))
                ai_circle(u, tp, 4.0f);
            else
                ai_stall(u, 15);
            return 1;
        }
        /* herido camina más rápido: +(100 - vida), con la vida entre 40 y 100 */
        ai_go(u, tp->x, tp->y, 10, 200 - (life < 40 ? 40 : life > 100 ? 100 : life), 1);
        return 1;
    case AI_WRAITH: /* aip: acercarse, quieto, pegar */
        if (combat) {
            if (ai_roll(u, 2)) {
                ai_attack(u, t, ANIM_ATTACK);
                return 1;
            }
        } else if (ai_roll(u, 0)) { /* WalkInRadiusToTarget: derecho hacia el jugador, hasta 12 */
            float step = d < 12.0f ? d : 12.0f, dl = d > 0.01f ? d : 1.0f;
            ai_go(u, u->x + (tp->x - u->x) / dl * step, u->y + (tp->y - u->y) / dl * step, 20, 100, 0);
            return 1;
        }
        ai_stall(u, u->aip[1]);
        return 1;
    case AI_CROGUE: /* aip: acercarse, quieto, pegar, velocidad al correr, correr */
        if (d <= 20.0f) { /* (20 - 3 * dificultad: Normal) */
            if (combat) {
                if (ai_roll(u, 2))
                    ai_attack(u, t, ANIM_ATTACK);
                else
                    ai_stall(u, u->aip[1]);
                return 1;
            }
            if (!ai_roll(u, 0)) {
                ai_stall(u, u->aip[1]);
                return 1;
            }
            if (!ai_roll(u, 4)) {
                ai_go(u, tp->x, tp->y, 10, 100, 1);
                return 1;
            }
        }
        ai_run_to(u, tp->x, tp->y, 8, 100 + u->aip[3], 1); /* RunToTargetUnitWithSteps(3) */
        return 1;
    case AI_CARCHER: /* aip: acercarse, disparar, quieto, correr, distancia para correr siempre,
                        habilidad 2, habilidad 3, distancia para caminar */
        if (!combat && hurt) { /* le pegaron de lejos: dispara enseguida */
            ai_attack(u, t, ANIM_ATTACK);
            return 1;
        }
        if (d < 6.0f && ai_roll(u, 3)) { /* muy cerca: se aleja corriendo (+100) */
            ai_escape(u, tp, 12.0f);
            u->ai_spd = 200;
            return 1;
        }
        if (u->aip[7] > 0 && d > u->aip[7] && ai_roll(u, 0)) {
            ai_go(u, tp->x, tp->y, u->aip[7], 110, 1); /* WalkToTargetUnitWithSteps, +10 */
            return 1;
        }
        if (d > u->aip[4]) {
            ai_run_to(u, tp->x, tp->y, u->aip[4], 200, 1); /* RunToTargetUnitWithSteps, +100 */
            return 1;
        }
        if (ai_roll(u, 1)) {
            ai_attack(u, t, ANIM_ATTACK); /* su flecha (no tiene habilidades) */
            return 1;
        }
        ai_stall(u, u->aip[2]);
        return 1;
    default:
        return 0;
    }
}
