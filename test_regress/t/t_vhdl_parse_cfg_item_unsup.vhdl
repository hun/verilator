-- DESCRIPTION: Verilator: an unexpected item in a block configuration is unsupported
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- Only for and use items may appear in a configuration.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
begin
end architecture rtl;

configuration c of t is
  for rtl
    constant k : integer := 1;
  end for;
end configuration c;
