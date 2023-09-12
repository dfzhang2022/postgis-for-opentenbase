<xsl:stylesheet version="1.0"
	xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
	xmlns:db="http://docbook.org/ns/docbook"
	exclude-result-prefixes="db"
>
<!-- ********************************************************************
	 ********************************************************************
	 Copyright 2010-2022, Regina Obe
	 License: BSD-3-Clause
   Purpose: This is an xsl transform that generates file list_curved_geometry_functions.xml.xsl which
   includes index listing of functions supporting curved geometries.
   It uses xml reference sections from reference.xml to then be processed by docbook
	 ******************************************************************** -->
	<xsl:output method="xml" indent="yes" encoding="utf-8" />

	<!-- We deal only with the reference chapter -->
	<xsl:template match="/">
		<xsl:apply-templates select="/db:book/db:chapter[@xml:id='reference']" />
	</xsl:template>

	<xsl:template match="//db:chapter">
				<itemizedlist>
			<!-- Pull out the purpose section for each ref entry and strip whitespace and put in a variable to be tagged unto each function comment  -->
				<xsl:for-each select='//db:refentry'>
					<xsl:sort select="@xml:id"/>
					<xsl:variable name='comment'>
						<xsl:value-of select="normalize-space(translate(translate(db:refnamediv/db:refpurpose,'&#x0d;&#x0a;', ' '), '&#09;', ' '))"/>
					</xsl:variable>
					<xsl:variable name="refid">
						<xsl:value-of select="@xml:id" />
					</xsl:variable>
					<xsl:variable name="refname">
						<xsl:value-of select="db:refnamediv/db:refname" />
					</xsl:variable>

			<!-- For each section if there is note that it implements Circular String catalog it -->
						<xsl:for-each select="db:refsection">
							<xsl:choose>
								<xsl:when test="descendant::node()[@conformance='curve']">
									<listitem><simpara><link linkend="{$refid}"><xsl:value-of select="$refname" /></link> - <xsl:value-of select="$comment" /></simpara></listitem>
								</xsl:when>
							</xsl:choose>
						</xsl:for-each>
				</xsl:for-each>
				</itemizedlist>
	</xsl:template>

</xsl:stylesheet>
