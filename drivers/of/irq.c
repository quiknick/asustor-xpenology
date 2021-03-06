/*
 *  Derived from arch/i386/kernel/irq.c
 *    Copyright (C) 1992 Linus Torvalds
 *  Adapted from arch/i386 by Gary Thomas
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *  Updated and modified by Cort Dougan <cort@fsmlabs.com>
 *    Copyright (C) 1996-2001 Cort Dougan
 *  Adapted for Power Macintosh by Paul Mackerras
 *    Copyright (C) 1996 Paul Mackerras (paulus@cs.anu.edu.au)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * This file contains the code used to make IRQ descriptions in the
 * device tree to actual irq numbers on an interrupt controller
 * driver.
 */

#include <linux/errno.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/string.h>
#include <linux/slab.h>

#if defined(CONFIG_SYNO_LSP_ARMADA)
/**
 * irq_of_parse_and_map - Parse and map an interrupt into linux virq space
 * @dev: Device node of the device whose interrupt is to be mapped
 * @index: Index of the interrupt to map
 *
 * This function is a wrapper that chains of_irq_parse_one() and
 * irq_create_of_mapping() to make things easier to callers
 */
unsigned int irq_of_parse_and_map(struct device_node *dev, int index)
{
	struct of_phandle_args oirq;

	if (of_irq_parse_one(dev, index, &oirq))
		return 0;

	return irq_create_of_mapping(&oirq);
}
#else /* CONFIG_SYNO_LSP_ARMADA */
/**
 * irq_of_parse_and_map - Parse and map an interrupt into linux virq space
 * @device: Device node of the device whose interrupt is to be mapped
 * @index: Index of the interrupt to map
 *
 * This function is a wrapper that chains of_irq_map_one() and
 * irq_create_of_mapping() to make things easier to callers
 */
unsigned int irq_of_parse_and_map(struct device_node *dev, int index)
{
	struct of_irq oirq;

	if (of_irq_map_one(dev, index, &oirq))
		return 0;

	return irq_create_of_mapping(oirq.controller, oirq.specifier,
				     oirq.size);
}
#endif /* CONFIG_SYNO_LSP_ARMADA */
EXPORT_SYMBOL_GPL(irq_of_parse_and_map);

/**
 * of_irq_find_parent - Given a device node, find its interrupt parent node
 * @child: pointer to device node
 *
 * Returns a pointer to the interrupt parent node, or NULL if the interrupt
 * parent could not be determined.
 */
struct device_node *of_irq_find_parent(struct device_node *child)
{
	struct device_node *p;
	const __be32 *parp;

	if (!of_node_get(child))
		return NULL;

	do {
		parp = of_get_property(child, "interrupt-parent", NULL);
		if (parp == NULL)
			p = of_get_parent(child);
		else {
			if (of_irq_workarounds & OF_IMAP_NO_PHANDLE)
				p = of_node_get(of_irq_dflt_pic);
			else
				p = of_find_node_by_phandle(be32_to_cpup(parp));
		}
		of_node_put(child);
		child = p;
	} while (p && of_get_property(p, "#interrupt-cells", NULL) == NULL);

	return p;
}

#if defined(CONFIG_SYNO_LSP_ARMADA)
/**
 * of_irq_parse_raw - Low level interrupt tree parsing
 * @parent:	the device interrupt parent
 * @addr:	address specifier (start of "reg" property of the device) in be32 format
 * @out_irq:	structure of_irq updated by this function
 *
 * Returns 0 on success and a negative number on error
 *
 * This function is a low-level interrupt tree walking function. It
 * can be used to do a partial walk with synthetized reg and interrupts
 * properties, for example when resolving PCI interrupts when no device
 * node exist for the parent. It takes an interrupt specifier structure as
 * input, walks the tree looking for any interrupt-map properties, translates
 * the specifier for each map, and then returns the translated map.
 */
int of_irq_parse_raw(const __be32 *addr, struct of_phandle_args *out_irq)
{
	struct device_node *ipar, *tnode, *old = NULL, *newpar = NULL;
	__be32 initial_match_array[MAX_PHANDLE_ARGS];
	const __be32 *match_array = initial_match_array;
	const __be32 *tmp, *imap, *imask, dummy_imask[] = { [0 ... MAX_PHANDLE_ARGS] = ~0 };

	u32 intsize = 1, addrsize, newintsize = 0, newaddrsize = 0;
	int imaplen, match, i;

#ifdef DEBUG
	of_print_phandle_args("of_irq_parse_raw: ", out_irq);
#endif

	ipar = of_node_get(out_irq->np);
#else /* CONFIG_SYNO_LSP_ARMADA */
/**
 * of_irq_map_raw - Low level interrupt tree parsing
 * @parent:	the device interrupt parent
 * @intspec:	interrupt specifier ("interrupts" property of the device)
 * @ointsize:   size of the passed in interrupt specifier
 * @addr:	address specifier (start of "reg" property of the device)
 * @out_irq:	structure of_irq filled by this function
 *
 * Returns 0 on success and a negative number on error
 *
 * This function is a low-level interrupt tree walking function. It
 * can be used to do a partial walk with synthetized reg and interrupts
 * properties, for example when resolving PCI interrupts when no device
 * node exist for the parent.
 */
int of_irq_map_raw(struct device_node *parent, const __be32 *intspec,
		   u32 ointsize, const __be32 *addr, struct of_irq *out_irq)
{
	struct device_node *ipar, *tnode, *old = NULL, *newpar = NULL;
	const __be32 *tmp, *imap, *imask;
	u32 intsize = 1, addrsize, newintsize = 0, newaddrsize = 0;
	int imaplen, match, i;

	pr_debug("of_irq_map_raw: par=%s,intspec=[0x%08x 0x%08x...],ointsize=%d\n",
		 parent->full_name, be32_to_cpup(intspec),
		 be32_to_cpup(intspec + 1), ointsize);

	ipar = of_node_get(parent);
#endif /* CONFIG_SYNO_LSP_ARMADA */

	/* First get the #interrupt-cells property of the current cursor
	 * that tells us how to interpret the passed-in intspec. If there
	 * is none, we are nice and just walk up the tree
	 */
	do {
		tmp = of_get_property(ipar, "#interrupt-cells", NULL);
		if (tmp != NULL) {
			intsize = be32_to_cpu(*tmp);
			break;
		}
		tnode = ipar;
		ipar = of_irq_find_parent(ipar);
		of_node_put(tnode);
	} while (ipar);
	if (ipar == NULL) {
		pr_debug(" -> no parent found !\n");
		goto fail;
	}

#if defined(CONFIG_SYNO_LSP_ARMADA)
	pr_debug("of_irq_parse_raw: ipar=%s, size=%d\n", of_node_full_name(ipar), intsize);

	if (out_irq->args_count != intsize)
#else /* CONFIG_SYNO_LSP_ARMADA */
	pr_debug("of_irq_map_raw: ipar=%s, size=%d\n", ipar->full_name, intsize);

	if (ointsize != intsize)
#endif /* CONFIG_SYNO_LSP_ARMADA */
		return -EINVAL;

	/* Look for this #address-cells. We have to implement the old linux
	 * trick of looking for the parent here as some device-trees rely on it
	 */
	old = of_node_get(ipar);
	do {
		tmp = of_get_property(old, "#address-cells", NULL);
		tnode = of_get_parent(old);
		of_node_put(old);
		old = tnode;
	} while (old && tmp == NULL);
	of_node_put(old);
	old = NULL;
	addrsize = (tmp == NULL) ? 2 : be32_to_cpu(*tmp);

	pr_debug(" -> addrsize=%d\n", addrsize);

#if defined(CONFIG_SYNO_LSP_ARMADA)
	/* Range check so that the temporary buffer doesn't overflow */
	if (WARN_ON(addrsize + intsize > MAX_PHANDLE_ARGS))
		goto fail;

	/* Precalculate the match array - this simplifies match loop */
	for (i = 0; i < addrsize; i++)
		initial_match_array[i] = addr ? addr[i] : 0;
	for (i = 0; i < intsize; i++)
		initial_match_array[addrsize + i] = cpu_to_be32(out_irq->args[i]);
#endif /* CONFIG_SYNO_LSP_ARMADA */

	/* Now start the actual "proper" walk of the interrupt tree */
	while (ipar != NULL) {
		/* Now check if cursor is an interrupt-controller and if it is
		 * then we are done
		 */
		if (of_get_property(ipar, "interrupt-controller", NULL) !=
				NULL) {
			pr_debug(" -> got it !\n");
#if defined(CONFIG_SYNO_LSP_ARMADA)
			// do nothing
#else /* CONFIG_SYNO_LSP_ARMADA */
			for (i = 0; i < intsize; i++)
				out_irq->specifier[i] =
						of_read_number(intspec +i, 1);
			out_irq->size = intsize;
			out_irq->controller = ipar;
#endif /* CONFIG_SYNO_LSP_ARMADA */
			of_node_put(old);
			return 0;
		}

#if defined(CONFIG_SYNO_LSP_ARMADA)
		/*
		 * interrupt-map parsing does not work without a reg
		 * property when #address-cells != 0
		 */
		if (addrsize && !addr) {
			pr_debug(" -> no reg passed in when needed !\n");
			goto fail;
		}
#endif /* CONFIG_SYNO_LSP_ARMADA */

		/* Now look for an interrupt-map */
		imap = of_get_property(ipar, "interrupt-map", &imaplen);
		/* No interrupt map, check for an interrupt parent */
		if (imap == NULL) {
			pr_debug(" -> no map, getting parent\n");
			newpar = of_irq_find_parent(ipar);
			goto skiplevel;
		}
		imaplen /= sizeof(u32);

		/* Look for a mask */
		imask = of_get_property(ipar, "interrupt-map-mask", NULL);

#if defined(CONFIG_SYNO_LSP_ARMADA)
		if (!imask)
			imask = dummy_imask;
#else /* CONFIG_SYNO_LSP_ARMADA */
		/* If we were passed no "reg" property and we attempt to parse
		 * an interrupt-map, then #address-cells must be 0.
		 * Fail if it's not.
		 */
		if (addr == NULL && addrsize != 0) {
			pr_debug(" -> no reg passed in when needed !\n");
			goto fail;
		}
#endif /* CONFIG_SYNO_LSP_ARMADA */

		/* Parse interrupt-map */
		match = 0;
		while (imaplen > (addrsize + intsize + 1) && !match) {
			/* Compare specifiers */
			match = 1;
#if defined(CONFIG_SYNO_LSP_ARMADA)
			for (i = 0; i < (addrsize + intsize); i++, imaplen--)
				match &= !((match_array[i] ^ *imap++) & imask[i]);
#else /* CONFIG_SYNO_LSP_ARMADA */
			for (i = 0; i < addrsize && match; ++i) {
				__be32 mask = imask ? imask[i]
						    : cpu_to_be32(0xffffffffu);
				match = ((addr[i] ^ imap[i]) & mask) == 0;
			}
			for (; i < (addrsize + intsize) && match; ++i) {
				__be32 mask = imask ? imask[i]
						    : cpu_to_be32(0xffffffffu);
				match =
				   ((intspec[i-addrsize] ^ imap[i]) & mask) == 0;
			}
			imap += addrsize + intsize;
			imaplen -= addrsize + intsize;
#endif /* CONFIG_SYNO_LSP_ARMADA */

			pr_debug(" -> match=%d (imaplen=%d)\n", match, imaplen);

			/* Get the interrupt parent */
			if (of_irq_workarounds & OF_IMAP_NO_PHANDLE)
				newpar = of_node_get(of_irq_dflt_pic);
			else
				newpar = of_find_node_by_phandle(be32_to_cpup(imap));
			imap++;
			--imaplen;

			/* Check if not found */
			if (newpar == NULL) {
				pr_debug(" -> imap parent not found !\n");
				goto fail;
			}

			/* Get #interrupt-cells and #address-cells of new
			 * parent
			 */
			tmp = of_get_property(newpar, "#interrupt-cells", NULL);
			if (tmp == NULL) {
				pr_debug(" -> parent lacks #interrupt-cells!\n");
				goto fail;
			}
			newintsize = be32_to_cpu(*tmp);
			tmp = of_get_property(newpar, "#address-cells", NULL);
			newaddrsize = (tmp == NULL) ? 0 : be32_to_cpu(*tmp);

			pr_debug(" -> newintsize=%d, newaddrsize=%d\n",
			    newintsize, newaddrsize);

			/* Check for malformed properties */
#if defined(CONFIG_SYNO_LSP_ARMADA)
			if (WARN_ON(newaddrsize + newintsize > MAX_PHANDLE_ARGS))
				goto fail;
#endif /* CONFIG_SYNO_LSP_ARMADA */
			if (imaplen < (newaddrsize + newintsize))
				goto fail;

			imap += newaddrsize + newintsize;
			imaplen -= newaddrsize + newintsize;

			pr_debug(" -> imaplen=%d\n", imaplen);
		}
		if (!match)
			goto fail;

#if defined(CONFIG_SYNO_LSP_ARMADA)
		/*
		 * Successfully parsed an interrrupt-map translation; copy new
		 * interrupt specifier into the out_irq structure
		 */
		of_node_put(out_irq->np);
		out_irq->np = of_node_get(newpar);

		match_array = imap - newaddrsize - newintsize;
		for (i = 0; i < newintsize; i++)
			out_irq->args[i] = be32_to_cpup(imap - newintsize + i);
		out_irq->args_count = intsize = newintsize;
		addrsize = newaddrsize;
#else /* CONFIG_SYNO_LSP_ARMADA */
		of_node_put(old);
		old = of_node_get(newpar);
		addrsize = newaddrsize;
		intsize = newintsize;
		intspec = imap - intsize;
		addr = intspec - addrsize;
#endif /* CONFIG_SYNO_LSP_ARMADA */

	skiplevel:
		/* Iterate again with new parent */
		pr_debug(" -> new parent: %s\n", of_node_full_name(newpar));
		of_node_put(ipar);
		ipar = newpar;
		newpar = NULL;
	}
 fail:
	of_node_put(ipar);
#if defined(CONFIG_SYNO_LSP_ARMADA)
	of_node_put(out_irq->np);
#else /* CONFIG_SYNO_LSP_ARMADA */
	of_node_put(old);
#endif /* CONFIG_SYNO_LSP_ARMADA */
	of_node_put(newpar);

	return -EINVAL;
}
#if defined(CONFIG_SYNO_LSP_ARMADA)
EXPORT_SYMBOL_GPL(of_irq_parse_raw);
#else /* CONFIG_SYNO_LSP_ARMADA */
EXPORT_SYMBOL_GPL(of_irq_map_raw);
#endif /* CONFIG_SYNO_LSP_ARMADA */

#if defined(CONFIG_SYNO_LSP_ARMADA)
/**
 * of_irq_parse_one - Resolve an interrupt for a device
 * @device: the device whose interrupt is to be resolved
 * @index: index of the interrupt to resolve
 * @out_irq: structure of_irq filled by this function
 *
 * This function resolves an interrupt for a node by walking the interrupt tree,
 * finding which interrupt controller node it is attached to, and returning the
 * interrupt specifier that can be used to retrieve a Linux IRQ number.
 */
int of_irq_parse_one(struct device_node *device, int index, struct of_phandle_args *out_irq)
#else /* CONFIG_SYNO_LSP_ARMADA */
/**
 * of_irq_map_one - Resolve an interrupt for a device
 * @device: the device whose interrupt is to be resolved
 * @index: index of the interrupt to resolve
 * @out_irq: structure of_irq filled by this function
 *
 * This function resolves an interrupt, walking the tree, for a given
 * device-tree node. It's the high level pendant to of_irq_map_raw().
 */
int of_irq_map_one(struct device_node *device, int index, struct of_irq *out_irq)
#endif /* CONFIG_SYNO_LSP_ARMADA */
{
	struct device_node *p;
	const __be32 *intspec, *tmp, *addr;
	u32 intsize, intlen;
#if defined(CONFIG_SYNO_LSP_ARMADA)
	int i, res = -EINVAL;

	pr_debug("of_irq_parse_one: dev=%s, index=%d\n", of_node_full_name(device), index);
#else /* CONFIG_SYNO_LSP_ARMADA */
	int res = -EINVAL;

	pr_debug("of_irq_map_one: dev=%s, index=%d\n", device->full_name, index);
#endif /* CONFIG_SYNO_LSP_ARMADA */

	/* OldWorld mac stuff is "special", handle out of line */
	if (of_irq_workarounds & OF_IMAP_OLDWORLD_MAC)
#if defined(CONFIG_SYNO_LSP_ARMADA)
		return of_irq_parse_oldworld(device, index, out_irq);

	/* Get the reg property (if any) */
	addr = of_get_property(device, "reg", NULL);
#else /* CONFIG_SYNO_LSP_ARMADA */
		return of_irq_map_oldworld(device, index, out_irq);
#endif /* CONFIG_SYNO_LSP_ARMADA */

	/* Get the interrupts property */
	intspec = of_get_property(device, "interrupts", &intlen);
#if defined(CONFIG_SYNO_LSP_ARMADA)
	if (intspec == NULL) {
		/* Try the new-style interrupts-extended */
		res = of_parse_phandle_with_args(device, "interrupts-extended",
						"#interrupt-cells", index, out_irq);
		if (res)
			return -EINVAL;
		return of_irq_parse_raw(addr, out_irq);
	}
#else /* CONFIG_SYNO_LSP_ARMADA */
	if (intspec == NULL)
		return -EINVAL;
#endif /* CONFIG_SYNO_LSP_ARMADA */
	intlen /= sizeof(*intspec);

	pr_debug(" intspec=%d intlen=%d\n", be32_to_cpup(intspec), intlen);

#if defined(CONFIG_SYNO_LSP_ARMADA)
	// do nothing
#else /* CONFIG_SYNO_LSP_ARMADA */
	/* Get the reg property (if any) */
	addr = of_get_property(device, "reg", NULL);
#endif /* CONFIG_SYNO_LSP_ARMADA */

	/* Look for the interrupt parent. */
	p = of_irq_find_parent(device);
	if (p == NULL)
		return -EINVAL;

	/* Get size of interrupt specifier */
	tmp = of_get_property(p, "#interrupt-cells", NULL);
	if (tmp == NULL)
		goto out;
	intsize = be32_to_cpu(*tmp);

	pr_debug(" intsize=%d intlen=%d\n", intsize, intlen);

	/* Check index */
	if ((index + 1) * intsize > intlen)
		goto out;

#if defined(CONFIG_SYNO_LSP_ARMADA)
	/* Copy intspec into irq structure */
	intspec += index * intsize;
	out_irq->np = p;
	out_irq->args_count = intsize;
	for (i = 0; i < intsize; i++)
		out_irq->args[i] = be32_to_cpup(intspec++);

	/* Check if there are any interrupt-map translations to process */
	res = of_irq_parse_raw(addr, out_irq);
#else /* CONFIG_SYNO_LSP_ARMADA */
	/* Get new specifier and map it */
	res = of_irq_map_raw(p, intspec + index * intsize, intsize,
			     addr, out_irq);
#endif /* CONFIG_SYNO_LSP_ARMADA */
 out:
	of_node_put(p);
	return res;
}
#if defined(CONFIG_SYNO_LSP_ARMADA)
EXPORT_SYMBOL_GPL(of_irq_parse_one);
#else /* CONFIG_SYNO_LSP_ARMADA */
EXPORT_SYMBOL_GPL(of_irq_map_one);
#endif /* CONFIG_SYNO_LSP_ARMADA */

/**
 * of_irq_to_resource - Decode a node's IRQ and return it as a resource
 * @dev: pointer to device tree node
 * @index: zero-based index of the irq
 * @r: pointer to resource structure to return result into.
 */
int of_irq_to_resource(struct device_node *dev, int index, struct resource *r)
{
	int irq = irq_of_parse_and_map(dev, index);

	/* Only dereference the resource if both the
	 * resource and the irq are valid. */
	if (r && irq) {
		const char *name = NULL;

#if defined(CONFIG_SYNO_LSP_ARMADA)
		memset(r, 0, sizeof(*r));
#endif /* CONFIG_SYNO_LSP_ARMADA */
		/*
		 * Get optional "interrupts-names" property to add a name
		 * to the resource.
		 */
		of_property_read_string_index(dev, "interrupt-names", index,
					      &name);

		r->start = r->end = irq;
#if defined(CONFIG_SYNO_LSP_ARMADA)
		r->flags = IORESOURCE_IRQ | irqd_get_trigger_type(irq_get_irq_data(irq));
		r->name = name ? name : of_node_full_name(dev);
#else /* CONFIG_SYNO_LSP_ARMADA */
		r->flags = IORESOURCE_IRQ;
		r->name = name ? name : dev->full_name;
#endif /* CONFIG_SYNO_LSP_ARMADA */
	}

	return irq;
}
EXPORT_SYMBOL_GPL(of_irq_to_resource);

/**
 * of_irq_count - Count the number of IRQs a node uses
 * @dev: pointer to device tree node
 */
int of_irq_count(struct device_node *dev)
{
#if defined(CONFIG_SYNO_LSP_ARMADA)
	struct of_phandle_args irq;
#endif /* CONFIG_SYNO_LSP_ARMADA */
	int nr = 0;

#if defined(CONFIG_SYNO_LSP_ARMADA)
	while (of_irq_parse_one(dev, nr, &irq) == 0)
#else /* CONFIG_SYNO_LSP_ARMADA */
	while (of_irq_to_resource(dev, nr, NULL))
#endif /* CONFIG_SYNO_LSP_ARMADA */
		nr++;

	return nr;
}

/**
 * of_irq_to_resource_table - Fill in resource table with node's IRQ info
 * @dev: pointer to device tree node
 * @res: array of resources to fill in
 * @nr_irqs: the number of IRQs (and upper bound for num of @res elements)
 *
 * Returns the size of the filled in table (up to @nr_irqs).
 */
int of_irq_to_resource_table(struct device_node *dev, struct resource *res,
		int nr_irqs)
{
	int i;

	for (i = 0; i < nr_irqs; i++, res++)
		if (!of_irq_to_resource(dev, i, res))
			break;

	return i;
}
EXPORT_SYMBOL_GPL(of_irq_to_resource_table);

struct intc_desc {
	struct list_head	list;
	struct device_node	*dev;
	struct device_node	*interrupt_parent;
};

/**
 * of_irq_init - Scan and init matching interrupt controllers in DT
 * @matches: 0 terminated array of nodes to match and init function to call
 *
 * This function scans the device tree for matching interrupt controller nodes,
 * and calls their initialization functions in order with parents first.
 */
void __init of_irq_init(const struct of_device_id *matches)
{
	struct device_node *np, *parent = NULL;
	struct intc_desc *desc, *temp_desc;
	struct list_head intc_desc_list, intc_parent_list;

	INIT_LIST_HEAD(&intc_desc_list);
	INIT_LIST_HEAD(&intc_parent_list);

	for_each_matching_node(np, matches) {
		if (!of_find_property(np, "interrupt-controller", NULL))
			continue;
		/*
		 * Here, we allocate and populate an intc_desc with the node
		 * pointer, interrupt-parent device_node etc.
		 */
		desc = kzalloc(sizeof(*desc), GFP_KERNEL);
		if (WARN_ON(!desc))
			goto err;

		desc->dev = np;
		desc->interrupt_parent = of_irq_find_parent(np);
		if (desc->interrupt_parent == np)
			desc->interrupt_parent = NULL;
		list_add_tail(&desc->list, &intc_desc_list);
	}

	/*
	 * The root irq controller is the one without an interrupt-parent.
	 * That one goes first, followed by the controllers that reference it,
	 * followed by the ones that reference the 2nd level controllers, etc.
	 */
	while (!list_empty(&intc_desc_list)) {
		/*
		 * Process all controllers with the current 'parent'.
		 * First pass will be looking for NULL as the parent.
		 * The assumption is that NULL parent means a root controller.
		 */
		list_for_each_entry_safe(desc, temp_desc, &intc_desc_list, list) {
			const struct of_device_id *match;
			int ret;
			of_irq_init_cb_t irq_init_cb;

			if (desc->interrupt_parent != parent)
				continue;

			list_del(&desc->list);
			match = of_match_node(matches, desc->dev);
			if (WARN(!match->data,
			    "of_irq_init: no init function for %s\n",
			    match->compatible)) {
				kfree(desc);
				continue;
			}

			pr_debug("of_irq_init: init %s @ %p, parent %p\n",
				 match->compatible,
				 desc->dev, desc->interrupt_parent);
			irq_init_cb = (of_irq_init_cb_t)match->data;
			ret = irq_init_cb(desc->dev, desc->interrupt_parent);
			if (ret) {
				kfree(desc);
				continue;
			}

			/*
			 * This one is now set up; add it to the parent list so
			 * its children can get processed in a subsequent pass.
			 */
			list_add_tail(&desc->list, &intc_parent_list);
		}

		/* Get the next pending parent that might have children */
#if defined(CONFIG_SYNO_LSP_ARMADA)
		desc = list_first_entry_or_null(&intc_parent_list,
						typeof(*desc), list);
		if (!desc) {
#else /* CONFIG_SYNO_LSP_ARMADA */
		desc = list_first_entry(&intc_parent_list, typeof(*desc), list);
		if (list_empty(&intc_parent_list) || !desc) {
#endif /* CONFIG_SYNO_LSP_ARMADA */
			pr_err("of_irq_init: children remain, but no parents\n");
			break;
		}
		list_del(&desc->list);
		parent = desc->dev;
		kfree(desc);
	}

	list_for_each_entry_safe(desc, temp_desc, &intc_parent_list, list) {
		list_del(&desc->list);
		kfree(desc);
	}
err:
	list_for_each_entry_safe(desc, temp_desc, &intc_desc_list, list) {
		list_del(&desc->list);
		kfree(desc);
	}
}
