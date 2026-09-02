#include "AnalysisPrompts.h"

#include <QJsonArray>
#include <QJsonDocument>

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

QJsonObject boolean(const QString &desc)
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")},
                       {QStringLiteral("description"), desc}};
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


QJsonObject digestBrief(const QString &paperId, const QString &title,
                        const QJsonObject &digest)
{
    auto claimText = [](const QJsonValue &v) {
        return v.toObject().value(QStringLiteral("text")).toString();
    };
    auto claimList = [&claimText](const QJsonArray &arr, int cap) {
        QJsonArray out;
        for (const QJsonValue &v : arr) {
            if (out.size() >= cap)
                break;
            const QString t = claimText(v);
            if (!t.isEmpty())
                out.append(t);
        }
        return out;
    };
    return QJsonObject{
        {QStringLiteral("paperId"), paperId},
        {QStringLiteral("title"), title},
        {QStringLiteral("oneLiner"),
         digest.value(QStringLiteral("oneLiner")).toString()},
        {QStringLiteral("problem"),
         claimText(digest.value(QStringLiteral("problem")))},
        {QStringLiteral("method"),
         claimText(digest.value(QStringLiteral("method")))},
        {QStringLiteral("results"),
         claimList(digest.value(QStringLiteral("results")).toArray(), 4)},
        {QStringLiteral("contributions"),
         claimList(digest.value(QStringLiteral("contributions")).toArray(), 4)},
        {QStringLiteral("limitations"),
         claimList(digest.value(QStringLiteral("limitations")).toArray(), 4)},
        {QStringLiteral("relevance"),
         digest.value(QStringLiteral("relevance"))
             .toObject()
             .value(QStringLiteral("level"))},
        {QStringLiteral("facets"), digest.value(QStringLiteral("facets"))}};
}

QJsonObject librarySchema(const QString &kind)
{
    auto idArray = [](const QString &desc) {
        return QJsonObject{
            {QStringLiteral("type"), QStringLiteral("array")},
            {QStringLiteral("description"), desc},
            {QStringLiteral("items"),
             QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}};
    };

    if (kind == QLatin1String("taxonomy")) {
        QJsonObject category = object(
            QJsonObject{
                {QStringLiteral("name"),
                 str(QStringLiteral("A short, concrete category name in the "
                                    "field's own words. Not \"Others\"."))},
                {QStringLiteral("description"),
                 str(QStringLiteral("One line saying what belongs here."))},
                {QStringLiteral("paperIds"),
                 idArray(QStringLiteral("Every paper that belongs in this "
                                        "category. A paper may appear in "
                                        "several categories, and in several "
                                        "dimensions."))}},
            {QStringLiteral("name"), QStringLiteral("paperIds")});
        QJsonObject dimension = object(
            QJsonObject{
                {QStringLiteral("dimension"),
                 enumStr({QStringLiteral("research_problem"),
                          QStringLiteral("scenario"),
                          QStringLiteral("paper_type"),
                          QStringLiteral("method_route"),
                          QStringLiteral("input_type"),
                          QStringLiteral("task_type"),
                          QStringLiteral("dataset"),
                          QStringLiteral("metric"),
                          QStringLiteral("contribution_type"),
                          QStringLiteral("main_limitation"),
                          QStringLiteral("relevance")},
                         QStringLiteral("Which way of cutting the library "
                                        "this is."))},
                {QStringLiteral("categories"),
                 QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                             {QStringLiteral("items"), category}}}},
            {QStringLiteral("dimension"), QStringLiteral("categories")});
        return object(
            QJsonObject{
                {QStringLiteral("dimensions"),
                 QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                             {QStringLiteral("description"),
                              QStringLiteral("One entry per dimension you can "
                                             "usefully cut this library by. "
                                             "Skip a dimension rather than "
                                             "inventing categories for it.")},
                             {QStringLiteral("items"), dimension}}},
                {QStringLiteral("ambiguous"),
                 QJsonObject{
                     {QStringLiteral("type"), QStringLiteral("array")},
                     {QStringLiteral("description"),
                      QStringLiteral("Papers you could not place confidently.")},
                     {QStringLiteral("items"),
                      object(QJsonObject{
                                 {QStringLiteral("paperId"), str(QStringLiteral("The paper."))},
                                 {QStringLiteral("note"),
                                  str(QStringLiteral("Why it is hard to place, "
                                                     "and the candidate categories."))}},
                             {QStringLiteral("paperId"), QStringLiteral("note")})}}}},
            {QStringLiteral("dimensions")});
    }

    if (kind == QLatin1String("map")) {
        QJsonObject route = object(
            QJsonObject{
                {QStringLiteral("route"),
                 str(QStringLiteral("The technical route, named the way the "
                                    "field names it."))},
                {QStringLiteral("paperIds"),
                 idArray(QStringLiteral("The papers taking it."))},
                {QStringLiteral("datasets"),
                 strArray(QStringLiteral("Data or environments it is evaluated "
                                         "on, across those papers."))},
                {QStringLiteral("metrics"),
                 strArray(QStringLiteral("Metrics it is judged by."))},
                {QStringLiteral("results"),
                 strArray(QStringLiteral("What it has achieved, concretely."))},
                {QStringLiteral("limitations"),
                 strArray(QStringLiteral("What it still cannot do."))}},
            {QStringLiteral("route"), QStringLiteral("paperIds")});
        QJsonObject question = object(
            QJsonObject{
                {QStringLiteral("question"),
                 str(QStringLiteral("A research question this library is "
                                    "circling."))},
                {QStringLiteral("routes"),
                 QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                             {QStringLiteral("items"), route}}}},
            {QStringLiteral("question"), QStringLiteral("routes")});
        return object(
            QJsonObject{
                {QStringLiteral("questions"),
                 QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                             {QStringLiteral("description"),
                              QStringLiteral("The main questions in this "
                                             "library, each with the routes "
                                             "taken at it.")},
                             {QStringLiteral("items"), question}}}},
            {QStringLiteral("questions")});
    }

    if (kind == QLatin1String("consensus")) {
        auto group = [&idArray](const QString &desc, bool twoSided) {
            QJsonObject props{
                {QStringLiteral("claim"), str(desc)},
                {QStringLiteral("paperIds"),
                 idArray(QStringLiteral("The papers involved."))},
                {QStringLiteral("note"),
                 str(QStringLiteral("What exactly they agree or disagree on, "
                                    "and under what conditions."))}};
            QStringList req{QStringLiteral("claim"), QStringLiteral("paperIds")};
            if (twoSided) {
                props.insert(QStringLiteral("opposingPaperIds"),
                             idArray(QStringLiteral("The papers on the other "
                                                    "side.")));
                props.insert(
                    QStringLiteral("kind"),
                    enumStr({QStringLiteral("real"), QStringLiteral("apparent"),
                             QStringLiteral("undecidable")},
                            QStringLiteral("real: they genuinely disagree. "
                                           "apparent: different conditions "
                                           "explain it. undecidable: not "
                                           "enough here to say.")));
                req << QStringLiteral("kind");
            }
            return QJsonObject{
                {QStringLiteral("type"), QStringLiteral("array")},
                {QStringLiteral("items"), object(props, req)}};
        };
        return object(
            QJsonObject{
                {QStringLiteral("agreed"),
                 group(QStringLiteral("Something several papers independently "
                                      "support."),
                       false)},
                {QStringLiteral("singleSource"),
                 group(QStringLiteral("Something only one paper supports."),
                       false)},
                {QStringLiteral("repeatedUnverified"),
                 group(QStringLiteral("Something repeated across papers but "
                                      "traced back to the same one source, or "
                                      "never independently tested."),
                       false)},
                {QStringLiteral("conflicts"),
                 group(QStringLiteral("Something the papers disagree about."),
                       true)}},
            {QStringLiteral("agreed"), QStringLiteral("conflicts")});
    }

    if (kind == QLatin1String("evolution")) {
        QJsonObject period = object(
            QJsonObject{
                {QStringLiteral("label"),
                 str(QStringLiteral("A name for this phase."))},
                {QStringLiteral("years"),
                 str(QStringLiteral("The years it covers, from the papers "
                                    "themselves. Say \"unknown\" if the "
                                    "interpretations do not carry years."))},
                {QStringLiteral("problems"),
                 strArray(QStringLiteral("What was being asked then."))},
                {QStringLiteral("methods"),
                 strArray(QStringLiteral("How it was being attacked."))},
                {QStringLiteral("data"),
                 strArray(QStringLiteral("What it was evaluated on."))},
                {QStringLiteral("evaluation"),
                 strArray(QStringLiteral("How success was measured."))},
                {QStringLiteral("representativePaperIds"),
                 idArray(QStringLiteral("The papers that stand for it."))},
                {QStringLiteral("turningPoint"),
                 str(QStringLiteral("What changed to end this phase, if "
                                    "anything in the library shows it."))}},
            {QStringLiteral("label"), QStringLiteral("representativePaperIds")});
        return object(
            QJsonObject{
                {QStringLiteral("periods"),
                 QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                             {QStringLiteral("items"), period}}},
                {QStringLiteral("longstanding"),
                 strArray(QStringLiteral("Problems present across the whole "
                                         "span and still not solved."))},
                {QStringLiteral("caveat"),
                 str(QStringLiteral("Say plainly if the library is too small "
                                    "or too narrow in time to show a "
                                    "trajectory at all."))}},
            {QStringLiteral("periods")});
    }

    if (kind == QLatin1String("coverage")) {
        auto notedArray = [&idArray](const QString &desc) {
            return QJsonObject{
                {QStringLiteral("type"), QStringLiteral("array")},
                {QStringLiteral("description"), desc},
                {QStringLiteral("items"),
                 object(QJsonObject{
                            {QStringLiteral("topic"), str(QStringLiteral("What."))},
                            {QStringLiteral("note"),
                             str(QStringLiteral("Why you say so."))},
                            {QStringLiteral("paperIds"),
                             idArray(QStringLiteral("The papers involved."))}},
                        {QStringLiteral("topic"), QStringLiteral("note")})}};
        };
        return object(
            QJsonObject{
                {QStringLiteral("wellCovered"),
                 notedArray(QStringLiteral("Topics this library covers well."))},
                {QStringLiteral("thin"),
                 notedArray(QStringLiteral("Topics with only a paper or two."))},
                {QStringLiteral("weakEvidence"),
                 notedArray(QStringLiteral("Conclusions resting on weak or "
                                           "single-source evidence."))},
                {QStringLiteral("missingComparisons"),
                 notedArray(QStringLiteral("Methods never compared against each "
                                           "other on common ground."))},
                {QStringLiteral("noRealWorldValidation"),
                 notedArray(QStringLiteral("Work never tested outside a "
                                           "benchmark."))},
                {QStringLiteral("noSharedMetric"),
                 notedArray(QStringLiteral("Questions with no metric everyone "
                                           "agrees on."))},
                {QStringLiteral("missingTypes"),
                 notedArray(QStringLiteral("Kinds of paper or experiment this "
                                           "library does not have at all."))},
                {QStringLiteral("disclaimer"),
                 str(QStringLiteral("A sentence, in your own words, making "
                                    "clear that this describes THIS "
                                    "collection of papers and not the field: "
                                    "work you have not collected is not work "
                                    "that does not exist."))}},
            {QStringLiteral("wellCovered"), QStringLiteral("thin"),
             QStringLiteral("disclaimer")});
    }

    if (kind == QLatin1String("opportunities")) {
        QJsonObject opp = object(
            QJsonObject{
                {QStringLiteral("question"),
                 str(QStringLiteral("A candidate research question, specific "
                                    "enough to act on."))},
                {QStringLiteral("sourcePaperIds"),
                 idArray(QStringLiteral("The papers it came out of."))},
                {QStringLiteral("gap"),
                 str(QStringLiteral("What existing work leaves unsolved."))},
                {QStringLiteral("approach"),
                 str(QStringLiteral("A plausible way at it."))},
                {QStringLiteral("minimalExperiment"),
                 str(QStringLiteral("The smallest experiment that would tell "
                                    "the reader whether this is worth "
                                    "pursuing -- days, not months."))},
                {QStringLiteral("baselines"),
                 strArray(QStringLiteral("What it would be measured against."))},
                {QStringLiteral("dataAndMetrics"),
                 strArray(QStringLiteral("Data, environments and metrics "
                                         "already available for it."))},
                {QStringLiteral("contribution"),
                 str(QStringLiteral("What contribution it could amount to."))},
                {QStringLiteral("risks"),
                 strArray(QStringLiteral("What could make it not work."))},
                {QStringLiteral("difficulty"),
                 enumStr({QStringLiteral("low"), QStringLiteral("medium"),
                          QStringLiteral("high")},
                         QStringLiteral("How hard, honestly."))},
                {QStringLiteral("confidence"),
                 enumStr({QStringLiteral("low"), QStringLiteral("medium"),
                          QStringLiteral("high")},
                         QStringLiteral("How much weight this deserves, given "
                                        "it rests only on the papers here."))},
                {QStringLiteral("gapType"),
                 enumStr({QStringLiteral("paper_left"),
                          QStringLiteral("library_gap"),
                          QStringLiteral("unverified_field")},
                         QStringLiteral("paper_left: a question a paper itself "
                                        "leaves open. library_gap: something "
                                        "this collection does not cover. "
                                        "unverified_field: you suspect the "
                                        "field has not done it, which nothing "
                                        "here can establish."))}},
            {QStringLiteral("question"), QStringLiteral("sourcePaperIds"),
             QStringLiteral("gap"), QStringLiteral("minimalExperiment"),
             QStringLiteral("difficulty"), QStringLiteral("confidence"),
             QStringLiteral("gapType")});
        return object(
            QJsonObject{
                {QStringLiteral("opportunities"),
                 QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                             {QStringLiteral("items"), opp}}},
                {QStringLiteral("disclaimer"),
                 str(QStringLiteral("A sentence making clear these are "
                                    "candidates read out of this library "
                                    "alone, not established gaps in the "
                                    "field, and that each needs a literature "
                                    "search before it is believed."))}},
            {QStringLiteral("opportunities"), QStringLiteral("disclaimer")});
    }

    if (kind == QLatin1String("classify")) {
        // §8.4: new papers are placed into the category system the reader has
        // already confirmed, rather than the system being redrawn under them.
        QJsonObject assignment = object(
            QJsonObject{
                {QStringLiteral("paperId"), str(QStringLiteral("The paper."))},
                {QStringLiteral("categoryIds"),
                 idArray(QStringLiteral("Ids of the existing categories it "
                                        "belongs to -- as many as apply. Use "
                                        "only ids from the list you were "
                                        "given."))},
                {QStringLiteral("ambiguous"),
                 boolean(QStringLiteral("True when it does not clearly belong "
                                        "anywhere, or belongs in two places "
                                        "that should not both be right."))},
                {QStringLiteral("note"),
                 str(QStringLiteral("One line, only when ambiguous."))}},
            {QStringLiteral("paperId"), QStringLiteral("categoryIds"),
             QStringLiteral("ambiguous")});
        return object(
            QJsonObject{
                {QStringLiteral("assignments"),
                 QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                             {QStringLiteral("items"), assignment}}}},
            {QStringLiteral("assignments")});
    }

    if (kind == QLatin1String("actions")) {
        auto withWhy = [&idArray](const QString &desc) {
            return QJsonObject{
                {QStringLiteral("type"), QStringLiteral("array")},
                {QStringLiteral("description"), desc},
                {QStringLiteral("items"),
                 object(QJsonObject{
                            {QStringLiteral("what"), str(QStringLiteral("The action."))},
                            {QStringLiteral("why"), str(QStringLiteral("Why now."))},
                            {QStringLiteral("paperIds"),
                             idArray(QStringLiteral("Papers it concerns, if "
                                                    "any."))}},
                        {QStringLiteral("what"), QStringLiteral("why")})}};
        };
        return object(
            QJsonObject{
                {QStringLiteral("readNext"),
                 withWhy(QStringLiteral("The papers to read closely next."))},
                {QStringLiteral("searchFor"),
                 withWhy(QStringLiteral("Kinds of paper worth searching for, "
                                        "given what is missing here."))},
                {QStringLiteral("reproduce"),
                 withWhy(QStringLiteral("Methods worth reproducing."))},
                {QStringLiteral("compare"),
                 withWhy(QStringLiteral("Papers worth putting side by side."))},
                {QStringLiteral("smallExperiments"),
                 withWhy(QStringLiteral("Small experiments worth running now."))},
                {QStringLiteral("advisorQuestions"),
                 withWhy(QStringLiteral("Questions to take to a supervisor."))}},
            {QStringLiteral("readNext")});
    }

    return object(QJsonObject{}, {});
}

namespace {

QString libraryInstruction(const QString &kind)
{
    if (kind == QLatin1String("taxonomy"))
        return QStringLiteral(
            "Sort this library into categories the reader could actually use, "
            "along several dimensions at once. A paper belongs wherever it "
            "belongs -- do not force one paper into one box. Name categories "
            "concretely; \"Other\" and \"Miscellaneous\" are failures. Say "
            "which papers you could not place rather than placing them badly.");
    if (kind == QLatin1String("map"))
        return QStringLiteral(
            "Draw the map of this library: the questions it is circling, the "
            "distinct routes taken at each question, which papers take which "
            "route, what each route is evaluated on, what it has achieved and "
            "where it stops.");
    if (kind == QLatin1String("consensus"))
        return QStringLiteral(
            "Find what these papers agree on, what rests on a single source, "
            "what is repeated without ever being independently tested, and "
            "what they actually contradict each other about. For a conflict, "
            "say whether it is real or an artefact of different conditions -- "
            "and where the evidence here cannot settle it, say so.");
    if (kind == QLatin1String("evolution"))
        return QStringLiteral(
            "Lay this library out in time: how the questions, the methods, the "
            "data and the standards of evidence moved, which papers mark the "
            "turns, and what has been unsolved throughout. Work only from what "
            "the interpretations carry -- if they do not carry years, say the "
            "trajectory cannot be established.");
    if (kind == QLatin1String("coverage"))
        return QStringLiteral(
            "Audit the collection itself: what it covers well, what it barely "
            "covers, which conclusions rest on thin evidence, which methods "
            "have never been compared fairly, what has never been tested "
            "outside a benchmark, and what kind of work is missing entirely.");
    if (kind == QLatin1String("opportunities"))
        return QStringLiteral(
            "Propose candidate research openings that follow from the "
            "limitations, conflicts and untested assumptions in these papers. "
            "Each one has to be actionable: a question, the smallest "
            "experiment that would test it, what to measure it against. Be "
            "honest about difficulty and about how much the evidence here "
            "really supports the idea.");
    if (kind == QLatin1String("classify"))
        return QStringLiteral(
            "Place each of these newly interpreted papers into the category "
            "system the reader has already settled on. Do not invent new "
            "categories and do not rename the ones you are given: if a paper "
            "does not fit, mark it ambiguous and say why. A paper can belong "
            "in several categories.");
    if (kind == QLatin1String("actions"))
        return QStringLiteral(
            "Say what the reader should do next with this library: what to "
            "read closely, what to search for, what to reproduce, what to "
            "compare, what small experiment to run, and what to ask a "
            "supervisor.");
    return QStringLiteral("Analyse this library.");
}

} // namespace

QString librarySystem(const QString &kind, const QString &lang,
                      const QString &profileBlock)
{
    QString s = QStringLiteral(
        "You are working across a researcher's whole collection of papers, in "
        "%1.\n\n%2\n\n"
        "What you are given is each paper's interpretation -- not its text. "
        "Work only from that.\n\n"
        "Rules:\n"
        "1. Refer to papers by the paperId you were given, always, so the "
        "reader can click through. Never invent a paper, an author or a "
        "number.\n"
        "2. Where the interpretations do not say, say they do not say. A "
        "confident answer built on absent evidence is worse than none.\n"
        "3. This collection is not the field. Anything you notice missing is "
        "missing HERE, and you must not present it as a gap in the "
        "literature.\n"
        "4. Be concrete and short. The reader is going to act on this.\n")
                    .arg(lang, libraryInstruction(kind));
    if (!profileBlock.isEmpty())
        s += QStringLiteral("\nDo it for this reader's project.\n\n%1")
                 .arg(profileBlock);
    return s;
}

QString libraryUser(const QString &kind, const QJsonArray &briefs,
                    const QJsonObject &extra)
{
    QString out = QStringLiteral("The %1 papers in this project, as they were "
                                 "interpreted:\n\n")
                      .arg(briefs.size());
    out += QString::fromUtf8(
        QJsonDocument(briefs).toJson(QJsonDocument::Indented));
    if (!extra.isEmpty()) {
        out += QStringLiteral("\n\nAlso relevant:\n");
        out += QString::fromUtf8(
            QJsonDocument(extra).toJson(QJsonDocument::Indented));
    }
    Q_UNUSED(kind);
    return out;
}

QJsonObject deepModuleSchema(const QString &moduleId)
{
    QJsonObject sectionItem = object(
        QJsonObject{
            {QStringLiteral("heading"),
             str(QStringLiteral("A short heading for this part."))},
            {QStringLiteral("items"),
             claimArraySchema(QStringLiteral("The points under it."))}},
        {QStringLiteral("heading"), QStringLiteral("items")});

    QJsonObject props{
        {QStringLiteral("summary"),
         str(QStringLiteral("One short paragraph orienting the reader before "
                            "the detail."))},
        {QStringLiteral("sections"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                     {QStringLiteral("description"),
                      QStringLiteral("The body of this module, two to six "
                                     "sections.")},
                     {QStringLiteral("items"), sectionItem}}}};
    QStringList required{QStringLiteral("summary"), QStringLiteral("sections")};

    if (moduleId == QLatin1String("background")) {
        props.insert(
            QStringLiteral("terms"),
            QJsonObject{
                {QStringLiteral("type"), QStringLiteral("array")},
                {QStringLiteral("description"),
                 QStringLiteral("The concepts someone new to the area has to "
                                "know before this paper makes sense.")},
                {QStringLiteral("items"),
                 object(QJsonObject{
                            {QStringLiteral("term"), str(QStringLiteral("The term."))},
                            {QStringLiteral("plain"),
                             str(QStringLiteral("What it means, in language a "
                                                "first-year graduate student "
                                                "would follow. No circular "
                                                "definitions."))},
                            {QStringLiteral("roleInPaper"),
                             str(QStringLiteral("What it is doing in THIS "
                                                "paper."))}},
                        {QStringLiteral("term"), QStringLiteral("plain"),
                         QStringLiteral("roleInPaper")})}});
        props.insert(QStringLiteral("prerequisites"),
                     strArray(QStringLiteral("What to read or learn first, if "
                                             "anything.")));
        required << QStringLiteral("terms");
    } else if (moduleId == QLatin1String("experiments")) {
        props.insert(
            QStringLiteral("setup"),
            object(QJsonObject{
                       {QStringLiteral("datasets"),
                        strArray(QStringLiteral("Datasets or environments, as "
                                                "named in the paper."))},
                       {QStringLiteral("baselines"),
                        strArray(QStringLiteral("What it is compared against."))},
                       {QStringLiteral("metrics"),
                        strArray(QStringLiteral("Metrics, as named."))},
                       {QStringLiteral("metricMeanings"),
                        strArray(QStringLiteral("One line per metric saying "
                                                "what it actually measures and "
                                                "which direction is better."))}},
                   {QStringLiteral("datasets"), QStringLiteral("baselines"),
                    QStringLiteral("metrics")}));
        props.insert(
            QStringLiteral("coverage"),
            object(QJsonObject{
                       {QStringLiteral("ablation"), boolean(QStringLiteral(
                            "Is there an ablation study?"))},
                       {QStringLiteral("robustness"), boolean(QStringLiteral(
                            "Robustness / sensitivity experiments?"))},
                       {QStringLiteral("generalization"), boolean(QStringLiteral(
                            "Held-out or cross-domain generalisation?"))},
                       {QStringLiteral("efficiency"), boolean(QStringLiteral(
                            "Cost, runtime or memory measurements?"))}},
                   {QStringLiteral("ablation"), QStringLiteral("robustness"),
                    QStringLiteral("generalization"),
                    QStringLiteral("efficiency")}));
        props.insert(QStringLiteral("supportsConclusions"),
                     claimSchema(QStringLiteral(
                         "Do the experiments actually support what the authors "
                         "conclude? Say where the gap is if there is one.")));
        required << QStringLiteral("setup") << QStringLiteral("coverage")
                 << QStringLiteral("supportsConclusions");
    } else if (moduleId == QLatin1String("repro")) {
        props.insert(
            QStringLiteral("checklist"),
            QJsonObject{
                {QStringLiteral("type"), QStringLiteral("array")},
                {QStringLiteral("description"),
                 QStringLiteral("One row per thing a reproduction needs: code, "
                                "data, model weights, hyper-parameters, "
                                "environment, evaluation code, external or "
                                "proprietary dependencies.")},
                {QStringLiteral("items"),
                 object(QJsonObject{
                            {QStringLiteral("what"), str(QStringLiteral("The thing."))},
                            {QStringLiteral("status"),
                             enumStr({QStringLiteral("available"),
                                      QStringLiteral("partial"),
                                      QStringLiteral("absent"),
                                      QStringLiteral("unclear")},
                                     QStringLiteral("Only what the paper says. "
                                                    "`unclear` when it does not "
                                                    "say -- never guess."))},
                            {QStringLiteral("detail"),
                             str(QStringLiteral("The URL, version or figure the "
                                                "paper gives, if any."))},
                            {QStringLiteral("evidence"), evidenceSchema()}},
                        {QStringLiteral("what"), QStringLiteral("status"),
                         QStringLiteral("evidence")})}});
        props.insert(QStringLiteral("blockers"),
                     strArray(QStringLiteral("What is missing that would stop "
                                             "someone reproducing this.")));
        required << QStringLiteral("checklist");
    } else if (moduleId == QLatin1String("limitations")) {
        props.insert(QStringLiteral("acknowledged"),
                     claimArraySchema(QStringLiteral(
                         "Limitations the authors state themselves.")));
        props.insert(QStringLiteral("additional"),
                     claimArraySchema(QStringLiteral(
                         "Limitations plainly visible in the work that the "
                         "authors do not raise.")));
        props.insert(QStringLiteral("validityRisks"),
                     claimArraySchema(QStringLiteral(
                         "Threats to validity in the data, the experiments or "
                         "the evaluation.")));
        props.insert(QStringLiteral("practical"),
                     claimArraySchema(QStringLiteral(
                         "Cost, latency, privacy, safety and deployment "
                         "constraints.")));
        required << QStringLiteral("acknowledged")
                 << QStringLiteral("additional");
    } else if (moduleId == QLatin1String("followups")) {
        props.insert(QStringLiteral("openQuestions"),
                     strArray(QStringLiteral("What is still unanswered after "
                                             "reading.")));
        props.insert(QStringLiteral("advisorQuestions"),
                     strArray(QStringLiteral("Questions worth putting to the "
                                             "authors or to a supervisor -- "
                                             "specific, answerable ones.")));
        props.insert(QStringLiteral("directions"),
                     strArray(QStringLiteral("Directions this opens up for the "
                                             "reader's own project.")));
        props.insert(
            QStringLiteral("minimalExperiments"),
            strArray(QStringLiteral("The smallest experiments that would test "
                                    "the interesting claims -- something "
                                    "runnable in days, not months.")));
        props.insert(QStringLiteral("references"),
                     strArray(QStringLiteral("References from this paper worth "
                                             "reading next, as cited.")));
        required << QStringLiteral("openQuestions");
    } else if (moduleId == QLatin1String("critique")) {
        props.insert(
            QStringLiteral("dimensions"),
            QJsonObject{
                {QStringLiteral("type"), QStringLiteral("array")},
                {QStringLiteral("description"),
                 QStringLiteral("One entry for each of: is the research "
                                "question clear; are the assumptions "
                                "reasonable; are the comparisons sufficient "
                                "and fair; do the experiments test each key "
                                "component; is the conclusion overstated; is "
                                "the improvement practically meaningful; would "
                                "it generalise; is it reproducible.")},
                {QStringLiteral("items"),
                 object(QJsonObject{
                            {QStringLiteral("dimension"),
                             str(QStringLiteral("Which of the eight."))},
                            {QStringLiteral("verdict"),
                             enumStr({QStringLiteral("solid"),
                                      QStringLiteral("adequate"),
                                      QStringLiteral("weak"),
                                      QStringLiteral("cannot_tell")},
                                     QStringLiteral("How it holds up."))},
                            {QStringLiteral("text"),
                             str(QStringLiteral("The judgement, in one or two "
                                                "sentences."))},
                            {QStringLiteral("source"),
                             enumStr({QStringLiteral("author_claim"),
                                      QStringLiteral("experimental"),
                                      QStringLiteral("ai_analysis"),
                                      QStringLiteral("speculation")},
                                     QStringLiteral("Almost always "
                                                    "ai_analysis here -- this "
                                                    "is your reading, not "
                                                    "theirs."))},
                            {QStringLiteral("evidence"), evidenceSchema()}},
                        {QStringLiteral("dimension"), QStringLiteral("verdict"),
                         QStringLiteral("text"), QStringLiteral("source"),
                         QStringLiteral("evidence")})}});
        required << QStringLiteral("dimensions");
    } else if (moduleId == QLatin1String("contributions")) {
        props.insert(
            QStringLiteral("contributions"),
            QJsonObject{
                {QStringLiteral("type"), QStringLiteral("array")},
                {QStringLiteral("items"),
                 object(QJsonObject{
                            {QStringLiteral("text"), str(QStringLiteral("The contribution."))},
                            {QStringLiteral("type"),
                             enumStr({QStringLiteral("problem"),
                                      QStringLiteral("method"),
                                      QStringLiteral("system"),
                                      QStringLiteral("dataset"),
                                      QStringLiteral("finding"),
                                      QStringLiteral("theory"),
                                      QStringLiteral("engineering")},
                                     QStringLiteral("What kind it is."))},
                            {QStringLiteral("source"),
                             enumStr({QStringLiteral("author_claim"),
                                      QStringLiteral("experimental"),
                                      QStringLiteral("ai_analysis"),
                                      QStringLiteral("speculation")},
                                     QStringLiteral("Where it comes from."))},
                            {QStringLiteral("evidence"), evidenceSchema()}},
                        {QStringLiteral("text"), QStringLiteral("type"),
                         QStringLiteral("source"),
                         QStringLiteral("evidence")})}});
        required << QStringLiteral("contributions");
    } else if (moduleId == QLatin1String("method")) {
        props.insert(
            QStringLiteral("steps"),
            QJsonObject{
                {QStringLiteral("type"), QStringLiteral("array")},
                {QStringLiteral("description"),
                 QStringLiteral("The method as an ordered walk-through.")},
                {QStringLiteral("items"),
                 object(QJsonObject{
                            {QStringLiteral("step"), str(QStringLiteral("What happens."))},
                            {QStringLiteral("novel"),
                             boolean(QStringLiteral("True when this step is "
                                                    "the paper's own "
                                                    "contribution, false when "
                                                    "it is taken from earlier "
                                                    "work."))},
                            {QStringLiteral("evidence"), evidenceSchema()}},
                        {QStringLiteral("step"), QStringLiteral("novel"),
                         QStringLiteral("evidence")})}});
        props.insert(QStringLiteral("inputs"),
                     strArray(QStringLiteral("What the method takes in.")));
        props.insert(QStringLiteral("outputs"),
                     strArray(QStringLiteral("What it produces.")));
        props.insert(QStringLiteral("workedExample"),
                     str(QStringLiteral("A small concrete example carried "
                                        "through the steps, with made-up but "
                                        "plausible numbers -- say clearly that "
                                        "the numbers are illustrative.")));
        required << QStringLiteral("steps");
    } else if (moduleId == QLatin1String("basics")) {
        props.insert(QStringLiteral("field"),
                     str(QStringLiteral("The research area, in a few words.")));
        props.insert(QStringLiteral("paperType"),
                     str(QStringLiteral("What kind of paper this is.")));
        props.insert(QStringLiteral("coreQuestion"),
                     claimSchema(QStringLiteral("The central question.")));
        props.insert(QStringLiteral("conclusions"),
                     claimArraySchema(QStringLiteral("The main conclusions.")));
        props.insert(QStringLiteral("positioning"),
                     claimSchema(QStringLiteral(
                         "Where this sits relative to the work around it.")));
        required << QStringLiteral("coreQuestion");
    }

    return object(props, required);
}

namespace {

QString moduleInstruction(const QString &id)
{
    if (id == QLatin1String("basics"))
        return QStringLiteral(
            "Establish what this paper is: the research area, the kind of "
            "paper, the central question, the main conclusions, and where it "
            "sits relative to the work it builds on and competes with.");
    if (id == QLatin1String("background"))
        return QStringLiteral(
            "Teach the reader what they need to know before the paper makes "
            "sense. Pull out the concepts the paper leans on, explain each in "
            "plain language a newcomer would follow, say what each one is "
            "doing in THIS paper, and list what is worth learning first.");
    if (id == QLatin1String("method"))
        return QStringLiteral(
            "Explain the method as an ordered walk-through: what goes in, what "
            "each stage does, what comes out. Mark which steps are this "
            "paper's own contribution and which are taken from earlier work. "
            "Explain the important figures, algorithms and equations in words. "
            "Finish with one small worked example -- illustrative numbers are "
            "fine as long as you say they are illustrative.");
    if (id == QLatin1String("experiments"))
        return QStringLiteral(
            "Take the experiments apart: what data or environments, against "
            "what baselines, under what metrics, and what each metric actually "
            "measures. Say what each experiment is there to establish and what "
            "the result was. Then answer plainly whether the experiments "
            "support the conclusions the authors draw.");
    if (id == QLatin1String("contributions"))
        return QStringLiteral(
            "List what this paper actually contributes, each labelled by kind. "
            "Separate what the authors claim from what the work demonstrates.");
    if (id == QLatin1String("critique"))
        return QStringLiteral(
            "Read the paper critically, one verdict per dimension. Be "
            "specific: name the missing baseline, the assumption that does not "
            "hold, the claim the experiment does not reach. Vague praise and "
            "vague suspicion are equally useless. Where you cannot tell from "
            "the text, say cannot_tell rather than guessing.");
    if (id == QLatin1String("limitations"))
        return QStringLiteral(
            "Separate the limitations the authors acknowledge from the ones "
            "they do not, and add the threats to validity in the data, the "
            "experiments and the evaluation, plus the practical constraints "
            "(cost, latency, privacy, safety, deployment).");
    if (id == QLatin1String("repro"))
        return QStringLiteral(
            "Answer one question: could someone reproduce this? Go through "
            "code, data, weights, hyper-parameters, environment, evaluation "
            "and any proprietary dependency. Report only what the paper says "
            "-- `unclear` is the honest answer where it says nothing, and a "
            "made-up repository URL is worse than no answer.");
    if (id == QLatin1String("followups"))
        return QStringLiteral(
            "Look forward. What is still unanswered, what would you put to the "
            "authors or to a supervisor, what does this open up for the "
            "reader's own project, what is the smallest experiment that would "
            "test the interesting claim, and which of its references are worth "
            "reading next.");
    return QStringLiteral("Interpret this part of the paper.");
}

} // namespace

QString deepSystem(const QString &lang, const QString &profileBlock,
                   const QString &moduleId)
{
    QString s = QStringLiteral(
        "You are reading one academic paper closely for a researcher, and "
        "writing one section of that close reading in %1.\n\n%2\n\n"
        "Rules that matter more than fluency:\n"
        "1. Every statement carries a `source`: `author_claim` for what the "
        "authors say, `experimental` for what an experiment in THIS paper "
        "shows, `ai_analysis` for your own reading, `speculation` for a guess.\n"
        "2. Every `author_claim` and `experimental` statement cites the "
        "paragraphs behind it by their [b<id>] marker with a short verbatim "
        "quote. Citations are checked against the paper afterwards: a quote "
        "that is not there is stripped and the statement is demoted, so never "
        "guess an id or reconstruct a quote from memory.\n"
        "3. Never invent page numbers, results, datasets, baselines, URLs or "
        "citations. Where the paper does not say, say that it does not say.\n"
        "4. Write for someone who will act on this: concrete, specific, and "
        "short enough to read.\n")
                    .arg(lang, moduleInstruction(moduleId));
    if (!profileBlock.isEmpty()) {
        s += QStringLiteral(
                 "\nThe reader is not reading this in the abstract. Tie what "
                 "you say to their project where it genuinely connects, and do "
                 "not force it where it does not.\n\n%1")
                 .arg(profileBlock);
    }
    return s;
}

QString deepUser(const QString &title, const QString &paperText, bool truncated,
                 const QJsonObject &digest)
{
    QString out;
    if (!title.isEmpty())
        out += QStringLiteral("Paper: %1\n\n").arg(title);
    if (!digest.isEmpty()) {
        const QString oneLiner =
            digest.value(QStringLiteral("oneLiner")).toString();
        if (!oneLiner.isEmpty()) {
            out += QStringLiteral(
                       "A quick reading of this paper already produced: %1\n"
                       "Do not simply restate it -- go deeper.\n\n")
                       .arg(oneLiner);
        }
    }
    if (truncated) {
        out += QStringLiteral(
            "NOTE: the paper was too long to include in full; the text below "
            "stops partway. Say so rather than filling the gap.\n\n");
    }
    out += QStringLiteral(
        "Extracted text. Each paragraph is prefixed with the id you must cite "
        "and the page it is on:\n\n");
    out += paperText;
    return out;
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
