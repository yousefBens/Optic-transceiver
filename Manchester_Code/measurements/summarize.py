"""Summarize physical CSV acquisitions without counting aggregates twice."""
import csv
from collections import defaultdict
from pathlib import Path

root = Path(__file__).resolve().parent
groups = defaultdict(list)
for path in sorted(root.glob('*.csv')):
    with path.open() as f:
        for raw in csv.DictReader(f):
            if raw.get('source') != 'hardware':
                continue
            row = {k: int(v) if v.isdigit() else v for k, v in raw.items()}
            groups[row['label']].append(row)

summaries = []
for label, rows in groups.items():
    duration = sum(r['duration_ms'] for r in rows)
    frames = sum(r['correct_frames'] for r in rows)
    summaries.append({'source': 'aggregate', 'label': label, 'runs': len(rows),
                      'half_bit_us': rows[0]['half_bit_us'], 'duration_ms': duration,
                      'correct_frames': frames, 'goodput_bps': round(frames * 24000 / duration, 2),
                      **{k: sum(r.get(k, 0) for r in rows) for k in
                         ['incorrect_frames', 'manchester_errors', 'preamble_errors', 'sfd_errors', 'gpio_errors', 'payload_bit_errors']}})
with (root / 'summary.csv').open('w', newline='') as f:
    writer = csv.DictWriter(f, fieldnames=list(summaries[0]))
    writer.writeheader()
    writer.writerows(summaries)

base = next(r for r in summaries if r['label'] == 'initial_firmware')
final = next((r for r in summaries if r['label'] == 'final_validation_3us'), None)
lines = ['# Résultats des essais physiques — 19 octobre 2026', '',
         'Nordic sur `/dev/ttyACM1` → STM32 sur `/dev/ttyACM0`, liaison telle qu’elle est branchée.', '',
         '## Profil retenu', '',
         '- Demi-bit TX/RX : **3 µs** ; débit logique : **166 666,7 bit/s**.',
         '- Payload utile : **3 octets** ; trame : **84 demi-bits**, soit **252 µs**.',
         '- TX : quartz HF + PWM0/EasyDMA ; RX : fronts PA9 horodatés par TIM2 + interruption prioritaire.',
         '- `sample_us=0` indique que le RX ne fait plus d’échantillonnage périodique.', '']
if final:
    lines += ['## Validation longue', '',
              f"**{final['runs']} acquisitions de 60 s**, total {final['duration_ms'] / 1000:.3f} s :",
              f"**{final['correct_frames']:,} trames correctes**, débit utile moyen **{final['goodput_bps']:,.2f} bit/s**.", '',
              f"Erreurs : Manchester **{final['manchester_errors']}**, préambule **{final['preamble_errors']}**, "
              f"SFD **{final['sfd_errors']}**, GPIO **{final['gpio_errors']}**, payload incorrect **{final['incorrect_frames']}** "
              f"et bits de payload erronés **{final['payload_bit_errors']}**.", '',
              f"Firmware initial : **{base['goodput_bps']:.2f} bit/s utiles**, {base['manchester_errors']} erreurs Manchester en 60 s. "
              f"Gain utile observé : **×{final['goodput_bps'] / base['goodput_bps']:.1f}**.", '',
              'Les deux cartes sont flashées avec le profil retenu. RX termine son acquisition puis s’arrête ; '
              'un reset lance une nouvelle acquisition de 60 s. Les acquisitions longues traversent le débordement de TIM2.', '']
lines += ['## Balayage et comparaisons', '',
          'Les essais exploratoires utilisent des durées différentes ; le débit utile est normalisé par leur durée. '
          'Les logs bruts et les CSV individuels permettent de retrouver chaque répétition.', '',
          '| Expérience | Demi-bit (µs) | Essais | Durée totale (s) | Trames correctes | Débit utile (bit/s) | Erreurs Manchester |',
          '|---|---:|---:|---:|---:|---:|---:|']
for r in summaries:
    lines.append(f"| {r['label']} | {r['half_bit_us']} | {r['runs']} | {r['duration_ms']/1000:.3f} | {r['correct_frames']} | {r['goodput_bps']:.2f} | {r['manchester_errors']} |")
lines += ['', '## Limites et fichiers', '',
          'À **2 µs**, aucune trame complète ne passe : ce réglage dépasse la limite de cette implémentation. '
          '3 µs est le meilleur réglage validé dans les conditions de ces essais ; il ne garantit pas le même résultat sur un autre canal.', '',
          '**Aucune perte exacte ne peut être annoncée sans numéro de séquence TX.** '
          'Le pourcentage théorique de succès inclut le décalage de l’horloge interne STM32 (~0,3 %) '
          'et les trames aux limites de la fenêtre. Zéro erreur détectée ne prouve pas zéro trame perdue. '
          'Le payload fixe FF 16 C1 permet de vérifier les trames complètes ; il n’y a pas de CRC.', '',
          '- [Notebook exécuté](analysis.ipynb)', '- [Graphiques de comparaison](comparison.html)',
          '- [Comparaison avant/après](validation.html)', '- [Synthèse CSV](summary.csv)',
          '- [Instructions de reproduction](README.md)', '- Données originales : `*.csv` et `raw/`.', '']
(root / 'RESULTS.md').write_text('\n'.join(lines))
print('Summarized', sum(len(v) for v in groups.values()), 'physical acquisitions')
if final:
    print(final)
