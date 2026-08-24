#include "AnalysisPrompts.h"

#include <QJsonArray>

namespace {

QJsonObject str(const QString &desc)
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                       {QStringLiteral("description"), desc}};
}

QJsonObject strArray(const QString &desc)
{
    return QJsonObject{
        {QStringLiteral("type"), QStringLiteral("array")},
        {QStringLiteral("description"), desc},
        {QStringLiteral("items"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}};
}

QJsonObject enumStr(const QStringList &values, const QString &desc)
{
    QJsonArray e;
    for (const QString &v : values)
        e.append(v);
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                       {QStringLiteral("enum"), e},
                       {QStringLiteral("description"), desc}};
}

QJsonObject object(const QJsonObject &props, const QStringList &required,
                   const QString &desc = QString())
{
    QJsonArray req;
    for (const QString &r : required)
        req.append(r);
    QJsonObject o{{QStringLiteral("type"), QStringLiteral("object")},
                  {QStringLiteral("properties"), props},
                  {QStringLiteral("required"), req}};
    if (!desc.isEmpty())
        o.insert(QStringLiteral("description"), desc);
    return o;
}

QJsonObject evidenceSchema()
{
    return QJsonObject{
        {QStringLiteral("type"), QStringLiteral("array")},
        {QStringLiteral("description"),
         QStringLiteral("Where in the paper this comes from. Cite the [b<id>] "
                        "markers you were given and quote the exact words you "
                        "are relying on. Leave empty only for your own "
                        "analysis.")},
        {QStringLiteral("items"),
         object(QJsonObject{
                    {QStringLiteral("blockId"),
                     QJsonObject{{QStringLiteral("type"),
                                  QStringLiteral("integer")},
                                 {QStringLiteral("description"),
                                  QStringLiteral("The number in the [b<id>] "
                                                 "marker of the paragraph.")}}},
                    {QStringLiteral("quote"),
                     str(QStringLiteral("A short verbatim span copied from "
                                        "that paragraph -- not a paraphrase."))}},
                {QStringLiteral("blockId"), QStringLiteral("quote")})}};
}

} // namespace

namespace AnalysisPrompts {

QString promptVersion() { return QStringLiteral("p1"); }

QString languageName(const QString &code)
{
    if (code.isEmpty()
        || code.compare(QStringLiteral("zh-CN"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("Simplified Chinese (zh-CN)");
    if (code.compare(QStringLiteral("zh-TW"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("Traditional Chinese (zh-TW)");
    if (code.compare(QStringLiteral("en"), Qt::CaseInsensitive) == 0
        || code.compare(QStringLiteral("en-US"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("English");
    if (code.compare(QStringLiteral("ja"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("Japanese");
    return code;
}

QJsonObject claimSchema(const QString &description)
{
    return object(
        QJsonObject{
            {QStringLiteral("text"), str(description)},
            {QStringLiteral("source"),
             enumStr({QStringLiteral("author_claim"),
                      QStringLiteral("experimental"),
                      QStringLiteral("ai_analysis"),
                      QStringLiteral("speculation")},
                     QStringLiteral(
                         "author_claim: the authors say so. experimental: an "
                         "experiment in this paper shows it. ai_analysis: your "
                         "own reading, not stated in the paper. speculation: a "
                         "guess you cannot support."))},
            {QStringLiteral("evidence"), evidenceSchema()}},
        {QStringLiteral("text"), QStringLiteral("source"),
         QStringLiteral("evidence")},
        description);
}

QJsonObject claimArraySchema(const QString &description)
{
    return QJsonObject{
        {QStringLiteral("type"), QStringLiteral("array")},
        {QStringLiteral("description"), description},
        {QStringLiteral("items"), claimSchema(description)}};
}

QJsonObject quickDigestSchema()
{
    QJsonObject contributionItem = object(
        QJsonObject{
            {QStringLiteral("text"), str(QStringLiteral("One contribution."))},
            {QStringLiteral("type"),
             enumStr({QStringLiteral("problem"), QStringLiteral("method"),
                      QStringLiteral("system"), QStringLiteral("dataset"),
                      QStringLiteral("finding"), QStringLiteral("theory"),
                      QStringLiteral("engineering")},
                     QStringLiteral("What kind of contribution it is."))},
            {QStringLiteral("source"),
             enumStr({QStringLiteral("author_claim"),
                      QStringLiteral("experimental"),
                      QStringLiteral("ai_analysis"),
                      QStringLiteral("speculation")},
                     QStringLiteral("Where this comes from."))},
            {QStringLiteral("evidence"), evidenceSchema()}},
        {QStringLiteral("text"), QStringLiteral("type"),
         QStringLiteral("source"), QStringLiteral("evidence")});

    QJsonObject priorityItem = object(
        QJsonObject{
            {QStringLiteral("what"),
             str(QStringLiteral("The section, figure, table or experiment to "
                                "read first, named as the paper names it."))},
            {QStringLiteral("why"), str(QStringLiteral("Why it matters most."))},
            {QStringLiteral("blockId"),
             QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                         {QStringLiteral("description"),
                          QStringLiteral("The [b<id>] of the heading or "
                                         "caption it starts at.")}}}},
        {QStringLiteral("what"), QStringLiteral("why"),
         QStringLiteral("blockId")});

    QJsonObject facets = object(
        QJsonObject{
            {QStringLiteral("researchProblem"),
             str(QStringLiteral("The research problem, in one short phrase."))},
            {QStringLiteral("scenario"),
             str(QStringLiteral("Application setting, one short phrase."))},
            {QStringLiteral("paperType"),
             enumStr({QStringLiteral("new_method"), QStringLiteral("system"),
                      QStringLiteral("dataset_benchmark"),
                      QStringLiteral("empirical_study"),
                      QStringLiteral("survey"), QStringLiteral("theory"),
                      QStringLiteral("application"), QStringLiteral("other")},
                     QStringLiteral("What kind of paper this is."))},
            {QStringLiteral("methodRoute"),
             str(QStringLiteral("The technical route it takes, one short "
                                "phrase -- the label you would group it under "
                                "against other papers."))},
            {QStringLiteral("inputTypes"),
             strArray(QStringLiteral("What the method consumes."))},
            {QStringLiteral("taskTypes"),
             strArray(QStringLiteral("The tasks it addresses."))},
            {QStringLiteral("datasets"),
             strArray(QStringLiteral("Datasets, benchmarks or experimental "
                                     "environments used, as named."))},
            {QStringLiteral("metrics"),
             strArray(QStringLiteral("Evaluation metrics, as named."))},
            {QStringLiteral("baselines"),
             strArray(QStringLiteral("What it is compared against, as named."))},
            {QStringLiteral("contributionTypes"),
             strArray(QStringLiteral("The contribution kinds present."))},
            {QStringLiteral("mainLimitations"),
             strArray(QStringLiteral("Its main limitations, short phrases."))},
            {QStringLiteral("year"),
             QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                         {QStringLiteral("description"),
                          QStringLiteral("Publication year if the text says "
                                         "so; 0 if it does not.")}}},
            {QStringLiteral("venue"),
             str(QStringLiteral("Venue if the text says so, else empty."))}},
        {QStringLiteral("researchProblem"), QStringLiteral("paperType"),
         QStringLiteral("methodRoute"), QStringLiteral("datasets"),
         QStringLiteral("metrics"), QStringLiteral("baselines"),
         QStringLiteral("mainLimitations")},
        QStringLiteral("Short, comparable labels. These are what the "
                       "project-wide analyses group and compare on, so keep "
                       "them terse and use the paper's own words."));

    QJsonObject props{
        {QStringLiteral("oneLiner"),
         str(QStringLiteral("The whole paper in one sentence."))},
        {QStringLiteral("problem"),
         claimSchema(QStringLiteral("The problem this paper attacks."))},
        {QStringLiteral("importance"),
         claimSchema(QStringLiteral("Why that problem matters."))},
        {QStringLiteral("method"),
         claimSchema(QStringLiteral("What they did, in two or three "
                                    "sentences."))},
        {QStringLiteral("results"),
         claimArraySchema(QStringLiteral("The main results. Give the numbers "
                                         "when the paper gives numbers."))},
        {QStringLiteral("contributions"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                     {QStringLiteral("description"),
                      QStringLiteral("The main contributions.")},
                     {QStringLiteral("items"), contributionItem}}},
        {QStringLiteral("limitations"),
         claimArraySchema(QStringLiteral("Clear limitations -- the ones the "
                                         "authors admit and the ones plainly "
                                         "visible in the work."))},
        {QStringLiteral("relevance"),
         object(QJsonObject{
                    {QStringLiteral("level"),
                     enumStr({QStringLiteral("high"), QStringLiteral("medium"),
                              QStringLiteral("low"), QStringLiteral("unclear")},
                             QStringLiteral("How relevant to the reader's "
                                            "project."))},
                    {QStringLiteral("reason"),
                     str(QStringLiteral("Why, in one or two sentences, in "
                                        "terms of the reader's project."))},
                    {QStringLiteral("evidence"), evidenceSchema()}},
                {QStringLiteral("level"), QStringLiteral("reason"),
                 QStringLiteral("evidence")})},
        {QStringLiteral("advice"),
         object(QJsonObject{
                    {QStringLiteral("code"),
                     enumStr({QStringLiteral("read_full"),
                              QStringLiteral("read_method_experiments"),
                              QStringLiteral("background"),
                              QStringLiteral("low_relevance"),
                              QStringLiteral("insufficient")},
                             QStringLiteral("What the reader should do with "
                                            "this paper."))},
                    {QStringLiteral("reason"),
                     str(QStringLiteral("One sentence saying why."))}},
                {QStringLiteral("code"), QStringLiteral("reason")})},
        {QStringLiteral("priority"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                     {QStringLiteral("description"),
                      QStringLiteral("Two to four places to read first.")},
                     {QStringLiteral("items"), priorityItem}}},
        {QStringLiteral("facets"), facets},
        {QStringLiteral("insufficient"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")},
                     {QStringLiteral("description"),
                      QStringLiteral("True when the extracted text is too "
                                     "broken or too short to interpret "
                                     "honestly.")}}},
        {QStringLiteral("insufficientReason"),
         str(QStringLiteral("What was missing, when insufficient is true."))}};

    return object(props,
                  {QStringLiteral("oneLiner"), QStringLiteral("problem"),
                   QStringLiteral("importance"), QStringLiteral("method"),
                   QStringLiteral("results"), QStringLiteral("contributions"),
                   QStringLiteral("limitations"), QStringLiteral("relevance"),
                   QStringLiteral("advice"), QStringLiteral("facets")});
}

QString quickSystem(const QString &lang, const QString &profileBlock)
{
    QString s = QStringLiteral(
        "You are helping a researcher decide what to do with a paper they "
        "have just added to their library. Read the extracted text and "
        "produce a short, concrete interpretation in %1.\n\n"
        "Rules that matter more than fluency:\n"
        "1. Every statement carries a `source`. Use `author_claim` only for "
        "what the authors actually say, `experimental` only for what an "
        "experiment in THIS paper shows, `ai_analysis` for your own reading, "
        "and `speculation` when you are guessing.\n"
        "2. Every `author_claim` and `experimental` statement must cite the "
        "paragraphs it rests on by their [b<id>] marker, with a short "
        "verbatim quote. A citation that does not match the paragraph will be "
        "stripped automatically and the statement demoted, so do not guess "
        "ids or invent quotes.\n"
        "3. Never invent page numbers, results, datasets, baselines or "
        "citations. If the text does not say, leave the field empty or say "
        "you cannot tell.\n"
        "4. If the extracted text is too broken or too short to interpret, "
        "set `insufficient` and say what was missing instead of writing a "
        "plausible-looking summary.\n"
        "5. `facets` are labels for grouping this paper against others in the "
        "same library. Keep them short and reuse the paper's own words.\n")
                    .arg(lang);
    if (!profileBlock.isEmpty()) {
        s += QStringLiteral(
                 "\nJudge relevance and reading advice against this reader's "
                 "project, not against the field in general.\n\n%1")
                 .arg(profileBlock);
    } else {
        s += QStringLiteral(
            "\nThe reader has not described their project yet, so judge "
            "relevance conservatively and prefer `unclear` over a confident "
            "guess.\n");
    }
    return s;
}

QString quickUser(const QString &title, const QString &paperText,
                  bool truncated)
{
    QString out;
    if (!title.isEmpty())
        out += QStringLiteral("Paper: %1\n\n").arg(title);
    if (truncated) {
        out += QStringLiteral(
            "NOTE: the paper was too long to include in full; the text below "
            "stops partway. Say so in `insufficientReason` if it stops before "
            "the results.\n\n");
    }
    out += QStringLiteral(
        "Extracted text. Each paragraph is prefixed with the id you must cite "
        "and the page it is on:\n\n");
    out += paperText;
    return out;
}

} // namespace AnalysisPrompts
