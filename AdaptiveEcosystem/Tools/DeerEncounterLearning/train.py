"""Development worker for a single region and parent policy. Python stdlib only."""

import json
import math
import os
from pathlib import Path
import sys


CONTRACT = "deer-encounter-2"
FEATURES = 8
ACTIONS = 3
GAMMA = 0.9
CLIP = 0.15
PROBE = (1.0, -0.2, 0.3, -0.4, 0.5, -0.6, 0.7, -0.8)
# Common visible encounter: 65% of sight range, energy 65%, damage pressure
# 70%, familiarity 15%, and closing speed 90 cm/s.
ENCOUNTER_REFERENCE = (1.0, *(math.tanh(x) for x in (0.3, 0.1, 0.3, 0.7, 0.15, -0.2, 0.4)))
RETREAT_BIAS = 2 * FEATURES


def dot(weights, features):
    return sum(weight * feature for weight, feature in zip(weights, features))


def probabilities(weights, features):
    logits = [
        dot(weights[a * FEATURES:(a + 1) * FEATURES], features)
        for a in range(ACTIONS)
    ]
    peak = max(logits)
    exps = [math.exp(logit - peak) for logit in logits]
    total = sum(exps)
    return [value / total for value in exps]


def train(parent, episodes):
    # Fit a fresh value baseline on the training episodes only. Bootstrap
    # fixed-length fragments with that baseline, then hold advantages fixed.
    critic = [0.0] * FEATURES
    for _ in range(6):
        for episode in episodes:
            next_x = episode.get("bootstrap")
            future = dot(critic, next_x) if next_x else 0.0
            for sample in reversed(episode["steps"]):
                future = 2.0 * sample["feed"] - 3.0 * sample["damage"] + GAMMA * future
                error = future - dot(critic, sample["x"])
                for i, feature in enumerate(sample["x"]):
                    critic[i] += 0.035 * error * feature

    pairs = []
    for episode in episodes:
        next_x = episode.get("bootstrap")
        future = dot(critic, next_x) if next_x else 0.0
        for sample in reversed(episode["steps"]):
            future = 2.0 * sample["feed"] - 3.0 * sample["damage"] + GAMMA * future
            pairs.append((sample, future - dot(critic, sample["x"])))

    mean = sum(adv for _, adv in pairs) / len(pairs)
    std = math.sqrt(sum((adv - mean) ** 2 for _, adv in pairs) / len(pairs))
    weights = parent.copy()
    for _ in range(24):
        gradient = [0.0] * len(weights)
        for sample, raw in pairs:
            x, action = sample["x"], sample["action"]
            advantage = (raw - mean) / max(std, 1e-5)
            policy = probabilities(weights, x)
            ratio = policy[action] / sample["p"]
            if (advantage > 0 and ratio > 1 + CLIP) or (advantage < 0 and ratio < 1 - CLIP):
                continue
            for a, p in enumerate(policy):
                scale = advantage * ratio * (int(a == action) - p)
                for i, feature in enumerate(x):
                    gradient[a * FEATURES + i] += scale * feature
        weights = [
            max(-8.0, min(8.0, weight + 0.08 * delta / len(pairs)))
            for weight, delta in zip(weights, gradient)
        ]
    return weights


def evaluate(parent, candidate, episodes):
    # One-decision off-policy estimate on independent real encounters.
    # This intentionally does not claim to estimate full Native trajectories.
    damage = feed = new_damage = new_feed = total = squares = kl = count = 0.0
    for episode in episodes:
        for sample in episode["steps"]:
            old = probabilities(parent, sample["x"])
            new = probabilities(candidate, sample["x"])
            action = sample["action"]
            if not math.isclose(old[action], sample["p"], abs_tol=1e-8):
                raise ValueError("recorded behavior probability differs from parent")
            ratio = new[action] / sample["p"]
            total += ratio
            squares += ratio ** 2
            damage += sample["damage"]
            feed += sample["feed"]
            new_damage += ratio * sample["damage"]
            new_feed += ratio * sample["feed"]
            kl += sum(p * math.log(p / q) for p, q in zip(old, new))
            count += 1
    if not count or (damage <= 0.0 and feed <= 0.0):
        return False, "holdout needs a confirmed hunting or feeding outcome"
    effective = total ** 2 / squares
    damage_delta = new_damage / total - damage / count if damage > 0.0 else None
    feed_delta = new_feed / total - feed / count if feed > 0.0 else None
    kl /= count
    tolerance = 0.001
    damage_ok = damage_delta is None or damage_delta <= tolerance
    feed_ok = feed_delta is None or feed_delta >= -tolerance
    improved = ((damage_delta is not None and damage_delta <= -tolerance)
                or (feed_delta is not None and feed_delta >= tolerance))
    accepted = effective >= 0.8 * count and kl <= 0.025 and damage_ok and feed_ok and improved
    old_retreat = probabilities(parent, PROBE)[2]
    new_retreat = probabilities(candidate, PROBE)[2]
    damage_result = "unobserved" if damage_delta is None else f"{damage_delta:+.5f}"
    feed_result = "unobserved" if feed_delta is None else f"{feed_delta:+.5f}"
    return accepted, (f"held-out one-step: damage_delta={damage_result} "
                      f"feeding_delta={feed_result} ESS={effective:.1f}/{count:.0f} KL={kl:.5f} "
                      f"probe_retreat={old_retreat:.4f}->{new_retreat:.4f}")


def outcome_label(outcome):
    # These are authored preferences for the next visible encounter. An
    # impact without a decision never becomes a fabricated PPO transition.
    if outcome["feeding"]:
        return 0.12, min(2.0 * outcome["fraction"], 1.0)
    return 0.80, min(3.0 * outcome["fraction"], 1.0)


def outcome_loss(retreat, outcomes):
    terms = []
    for outcome in outcomes:
        target, weight = outcome_label(outcome)
        terms.append(-weight * (target * math.log(retreat) + (1 - target) * math.log1p(-retreat)))
    return sum(terms) / len(terms)


def fit_outcomes(parent, outcomes):
    train_set = [outcome for i, outcome in enumerate(outcomes, 1) if i % 3]
    holdout = [outcome for i, outcome in enumerate(outcomes, 1) if not i % 3]
    weights = parent.copy()
    initial = parent[RETREAT_BIAS]
    for _ in range(12):
        retreat = probabilities(weights, ENCOUNTER_REFERENCE)[2]
        gradient = sum(weight * (retreat - target)
                       for target, weight in map(outcome_label, train_set)) / len(train_set)
        weights[RETREAT_BIAS] = min(8.0, initial + 0.30,
                                   max(-8.0, initial - 0.30, weights[RETREAT_BIAS] - 0.10 * gradient))

    old = probabilities(parent, ENCOUNTER_REFERENCE)
    new = probabilities(weights, ENCOUNTER_REFERENCE)
    loss_delta = outcome_loss(new[2], holdout) - outcome_loss(old[2], holdout)
    kl = sum(p * math.log(p / q) for p, q in zip(old, new))
    damage = [outcome for outcome in holdout if not outcome["feeding"]]
    feeding = [outcome for outcome in holdout if outcome["feeding"]]
    # When both behaviors are represented, bound degradation for either one.
    groups_ok = all(outcome_loss(new[2], group) - outcome_loss(old[2], group) <= 0.025
                    for group in (damage, feeding) if group)
    accepted = loss_delta <= -0.001 and kl <= 0.025 and groups_ok
    return weights, accepted, (f"regional outcomes: loss_delta={loss_delta:+.5f} "
                               f"reference_retreat={old[2]:.4f}->{new[2]:.4f} KL={kl:.5f} "
                               f"train={len(train_set)} held_damage={len(damage)} held_feeding={len(feeding)}")


def main(batch_path, result_path):
    batch = json.loads(batch_path.read_text(encoding="utf-8"))
    if batch["contract"] != CONTRACT:
        raise ValueError("unknown deer encounter contract")
    parent = batch["weights"]
    candidate, accepted, message = fit_outcomes(parent, batch["outcomes"])
    if not accepted:
        train_set = [episode for episode in batch["episodes"] if not episode["holdout"]]
        holdout = [episode for episode in batch["episodes"] if episode["holdout"]]
        train_steps = [step for episode in train_set for step in episode["steps"]]
        holdout_steps = [step for episode in holdout for step in episode["steps"]]
        if (len(train_set) >= 12 and len(holdout) >= 6 and len(train_steps) + len(holdout_steps) >= 72
                and any(step["damage"] or step["feed"] for step in train_steps)
                and any(step["damage"] or step["feed"] for step in holdout_steps)):
            decision_candidate = train(parent, train_set)
            decision_accepted, decision_message = evaluate(parent, decision_candidate, holdout)
            if decision_accepted:
                candidate, accepted, message = decision_candidate, True, decision_message
    result = {"contract": CONTRACT, "token": batch["token"], "accepted": accepted, "message": message}
    if accepted:
        result.update(weights=candidate, probe=probabilities(candidate, PROBE))
    temporary = result_path.with_suffix(".tmp")
    temporary.write_text(json.dumps(result, allow_nan=False), encoding="utf-8")
    os.replace(temporary, result_path)


if __name__ == "__main__":
    main(Path(sys.argv[1]), Path(sys.argv[2]))
