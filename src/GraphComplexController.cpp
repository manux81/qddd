#include "GraphComplexController.h"

/* -------------------------------------------------------
 * Utility: ricerca ricorsiva nodo per varId
 * -----------------------------------------------------*/
static VarNode* findNodeById(VarNode* n, const QString& id)
{
    if (!n) return nullptr;
    if (n->varId == id) return n;

    for (VarNode* c : n->children) {
        if (auto* r = findNodeById(c, id))
            return r;
    }
    return nullptr;
}

/* -------------------------------------------------------
 * Costruzione flat dei campi (tree-view style)
 * -----------------------------------------------------*/
static void buildFields(VarNode* n, QVector<GraphNodeField>& out, int depth)
{
    if (!n) return;

    GraphNodeField f;
    f.id           = n->varId;
    f.name         = n->name;
    f.depth        = depth;
    f.isExpandable = !n->children.isEmpty();
    f.expanded     = n->expanded;
    f.targetId     = n->type.contains('*') ? n->value : QString();

    // valore solo se nodo chiuso
    f.value = n->expanded ? QString() : n->value;

    out.push_back(f);

    if (!n->expanded)
        return;

    for (VarNode* c : n->children)
        buildFields(c, out, depth + 1);
}




/* =======================================================
 * GraphComplexController
 * =====================================================*/
GraphComplexController::GraphComplexController(
    DebugSession *session,
    GraphicalVariablesView *view,
    QObject *parent
)
    : QObject(parent),
      m_session(session),
      m_view(view)
{
    connect(m_session, &DebugSession::complexVariablesUpdated,
            this, &GraphComplexController::onComplexVars);

    connect(m_view, &GraphicalVariablesView::toggleNodeExpanded,
            this, &GraphComplexController::onToggleExpanded);

    connect(m_view, &GraphicalVariablesView::nodeDoubleClicked,
            this, &GraphComplexController::onNodeDblClicked);
}

/* -------------------------------------------------------
 * Rebuild completo del grafo
 * -----------------------------------------------------*/
void GraphComplexController::onComplexVars(QList<VarNode*> roots)
{
    QVector<GraphNode> nodes;
    QVector<GraphEdge> edges;

    for (VarNode* root : roots) {
        if (!root)
            continue;

        GraphNode g;
        g.id    = root->varId;
        g.title = root->name;
        g.color = typeToColor(root->type);

        buildFields(root, g.fields, 0);
        nodes.push_back(g);
    }

    m_view->setGraph(nodes, edges);
}

/* -------------------------------------------------------
 * Toggle espansione (ricorsivo)
 * -----------------------------------------------------*/
void GraphComplexController::onToggleExpanded(const QString& id)
{
    for (VarNode* r : m_session->complexVariables()) {
        if (auto* n = findNodeById(r, id)) {
            n->expanded = !n->expanded;

            emit m_session->complexVariablesUpdated(
                m_session->complexVariables()
            );
            return;
        }
    }
}

/* -------------------------------------------------------
 * Colore per tipo
 * -----------------------------------------------------*/
QColor GraphComplexController::typeToColor(const QString &type)
{
    const QString t = type.toLower();
    if (t.contains("int"))
        return QColor("#BBDEFB");
    if (t.contains("float") || t.contains("double"))
        return QColor("#FFCCBC");
    if (t.contains("char") || t.contains("string"))
        return QColor("#C8E6C9");
    if (t.contains('*') || t.contains("&"))
        return QColor("#F8BBD0");
    return QColor("#E0E0E0");
}

/* -------------------------------------------------------
 * Hook futuro (lazy loading, ecc.)
 * -----------------------------------------------------*/
void GraphComplexController::onNodeDblClicked(const QString &id)
{
    Q_UNUSED(id);
}
