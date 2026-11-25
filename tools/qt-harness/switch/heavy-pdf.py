# Writes a PDF whose pages are slow for PDFium to draw: a dense vector
# scatter plot on every page, the kind of figure an ML paper is full of.
#   python3 heavy-pdf.py out.pdf <pages> <points-per-page> <seed>
import random, sys

out, pages, points, seed = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
random.seed(seed)
objs = []                       # bodies, 1-based object numbers

def add(body):
    objs.append(body)
    return len(objs)

font = add(b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")
page_ids = []
pages_id = add(b"")             # placeholder, filled in below
for p in range(pages):
    ops = [b"BT /F1 14 Tf 72 740 Td (Figure %d: a scatter plot PDFium draws one dot at a time) Tj ET" % (p + 1)]
    for _ in range(points):
        x = 72 + random.random() * 468
        y = 100 + random.random() * 600
        r, g = random.random(), random.random()
        # A translucent stroked circle: four Beziers, blended -- the slow path.
        k = 1.2
        ops.append(b"/A gs %.3f %.3f 0.5 rg %.3f 0.2 %.3f RG %.2f %.2f m "
                   b"%.2f %.2f %.2f %.2f %.2f %.2f c %.2f %.2f %.2f %.2f %.2f %.2f c "
                   b"%.2f %.2f %.2f %.2f %.2f %.2f c %.2f %.2f %.2f %.2f %.2f %.2f c B"
                   % (r, g, g, r, x + 2, y,
                      x + 2, y + k, x + k, y + 2, x, y + 2,
                      x - k, y + 2, x - 2, y + k, x - 2, y,
                      x - 2, y - k, x - k, y - 2, x, y - 2,
                      x + k, y - 2, x + 2, y - k, x + 2, y))
    stream = b"\n".join(ops)
    content = add(b"<< /Length %d >>\nstream\n" % len(stream) + stream + b"\nendstream")
    page_ids.append(add(b"<< /Type /Page /Parent %d 0 R /MediaBox [0 0 612 792] "
                        b"/Resources << /Font << /F1 %d 0 R >> /ExtGState << /A << /ca 0.6 /CA 0.6 >> >> >> /Contents %d 0 R >>"
                        % (pages_id, font, content)))
objs[pages_id - 1] = (b"<< /Type /Pages /Count %d /Kids [%s] >>"
                      % (pages, b" ".join(b"%d 0 R" % i for i in page_ids)))
catalog = add(b"<< /Type /Catalog /Pages %d 0 R >>" % pages_id)

data = bytearray(b"%PDF-1.4\n")
offsets = []
for i, body in enumerate(objs, 1):
    offsets.append(len(data))
    data += b"%d 0 obj\n" % i + body + b"\nendobj\n"
xref = len(data)
data += b"xref\n0 %d\n0000000000 65535 f \n" % (len(objs) + 1)
for off in offsets:
    data += b"%010d 00000 n \n" % off
data += b"trailer\n<< /Size %d /Root %d 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (len(objs) + 1, catalog, xref)
open(out, "wb").write(data)
