"""Execute analysis cells and save notebook outputs plus a standalone Plotly HTML."""
from contextlib import redirect_stdout
import io
import json
import os
from pathlib import Path

root = Path(__file__).resolve().parent
os.chdir(root)
path = root / 'analysis.ipynb'
notebook = json.loads(path.read_text())
namespace = {'__name__': '__main__'}
count = 0
for cell in notebook['cells']:
    if cell['cell_type'] != 'code':
        continue
    count += 1
    output = io.StringIO()
    source = ''.join(cell['source']).replace('    fig.show()', '    # Figure is embedded below by render_analysis.py')
    with redirect_stdout(output):
        exec(compile(source, str(path), 'exec'), namespace)
    cell['execution_count'] = count
    cell['outputs'] = []
    if output.getvalue():
        cell['outputs'].append({'output_type': 'stream', 'name': 'stdout', 'text': output.getvalue().splitlines(True)})
    if 'fig.write_html' in source and 'fig' in namespace:
        fig = namespace['fig']
        # Bundled JS permits offline viewing without modifying the Python environment.
        cell['outputs'].append({'output_type': 'display_data', 'metadata': {},
                                'data': {'text/html': [fig.to_html(full_html=False, include_plotlyjs=True)]}})
path.write_text(json.dumps(notebook, ensure_ascii=False, indent=2))
print('Updated notebook and comparison.html with', len(namespace.get('rows', [])), 'physical acquisitions')
