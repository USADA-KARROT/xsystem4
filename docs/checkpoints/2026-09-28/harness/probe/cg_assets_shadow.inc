/* Appended to a copy of src/asset_manager.c by build_probe.py (the probe has
 * no archives): CG files made in memory, served to the asset manager by name
 * and by number. Nothing here is part of the engine. */
struct probe_cg_file {
	const char *name;
	const uint8_t *data;
	size_t size;
};

static const struct probe_cg_file *probe_cg_files;
static int probe_nr_cg_files;
static struct archive_ops probe_cg_ops;
static struct archive probe_cg_archive;
static struct asset_manager probe_cg_manager;

static void probe_cg_free_data(struct archive_data *data)
{
	free(data->data);
	free(data);
}

static bool probe_cg_exists_by_id(struct asset_manager *manager, int id)
{
	(void)manager;
	return id >= 0 && id < probe_nr_cg_files;
}

static bool probe_cg_exists_by_name(struct asset_manager *manager, const char *name, int *id_out)
{
	(void)manager;
	for (int i = 0; i < probe_nr_cg_files; i++) {
		if (!strcmp(probe_cg_files[i].name, name)) {
			if (id_out)
				*id_out = i;
			return true;
		}
	}
	return false;
}

static struct archive_data *probe_cg_get_by_id(struct asset_manager *manager, int id)
{
	if (!probe_cg_exists_by_id(manager, id))
		return NULL;
	struct archive_data *data = xcalloc(1, sizeof(struct archive_data));
	data->size = probe_cg_files[id].size;
	data->data = xmalloc(data->size ? data->size : 1);
	memcpy(data->data, probe_cg_files[id].data, data->size);
	data->no = id;
	data->archive = &probe_cg_archive;
	return data;
}

static struct archive_data *probe_cg_get_by_name(struct asset_manager *manager, const char *name, int *id_out)
{
	int id;
	if (!probe_cg_exists_by_name(manager, name, &id))
		return NULL;
	if (id_out)
		*id_out = id;
	return probe_cg_get_by_id(manager, id);
}

void probe_install_cg_assets(const struct probe_cg_file *files, int n)
{
	probe_cg_files = files;
	probe_nr_cg_files = n;
	probe_cg_ops.free_data = probe_cg_free_data;
	probe_cg_archive.ops = &probe_cg_ops;
	probe_cg_manager.exists_by_id = probe_cg_exists_by_id;
	probe_cg_manager.exists_by_name = probe_cg_exists_by_name;
	probe_cg_manager.get_by_id = probe_cg_get_by_id;
	probe_cg_manager.get_by_name = probe_cg_get_by_name;
	assets[ASSET_CG] = &probe_cg_manager;
}
